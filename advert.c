#include "advert.h"
#include "error.h"
#include "spin.h"
#include "thread.h"
#include "xalloc.h"
#include <stdio.h>
#include <sys/socket.h>

#define SEND_DELAY_USEC (3 * 1000000)

struct notice
{
	sender_handle sender;
	char* json_desc;
	struct notice* next;
};

struct advert
{
	sock_handle mcast_sock;
	sock_addr_handle mcast_addr;
	thread_handle mcast_thr;
	char* json_msg;
	char* env;
	size_t json_sz;
	struct notice* notices;
	volatile int lock;
};

static const char* escape_quotes(const char* in)
{
	static char buf[512];
	char* out = buf;

	while (*in) {
		if (*in == '"')
			*out++ = '\\';

		*out++ = *in++;
	}

	*out = '\0';
	return buf;
}

static status make_json_map(advert_handle advert)
{
	char buf[8192], hostname[256];
	struct notice* it;
	char* new_msg;
	status st;

	strcpy(buf, "{\"hostname\":\"");

	if (FAILED(st = sock_get_hostname(hostname, sizeof(hostname))))
		return st;

	strcat(buf, escape_quotes(hostname));
	strcat(buf, "\", \"env\":\"");
	strcat(buf, escape_quotes(advert->env));
	strcat(buf, "\", \"version\":\"" WIRE_VERSION "\"");
	strcat(buf, ", \"data\":[");

	for (it = advert->notices; it; it = it->next) {
		strcat(buf, it->json_desc);
		if (it->next)
			strcat(buf, ", ");
	}

	strcat(buf, "]}");

	new_msg = xstrdup(buf);
	if (!new_msg)
		return NO_MEMORY;

	SPIN_WRITE_LOCK(&advert->lock, no_rev);

	XFREE(advert->json_msg);
	advert->json_msg = new_msg;
	advert->json_sz = strlen(advert->json_msg) + 1;

	SPIN_UNLOCK(&advert->lock, no_rev);
	return OK;
}

static status make_json_notice(sender_handle sender, char** pjson)
{
	char buf[512];
	int n = sprintf(buf, "{\"port\":%d, \"description\":\"%s\"}",
					sender_get_listen_port(sender),
					escape_quotes(
						storage_get_description(sender_get_storage(sender))));
	if (n < 0)
		return error_errno("sprintf");

	*pjson = xstrdup(buf);
	return *pjson ? OK : NO_MEMORY;
}

static void* mcast_func(thread_handle thr)
{
	advert_handle advert = thread_get_param(thr);
	status st = OK, st2;

	while (!thread_is_stopping(thr)) {
		SPIN_WRITE_LOCK(&advert->lock, no_rev);
		if (advert->json_msg)
			st = sock_sendto(advert->mcast_sock, advert->mcast_addr,
							 advert->json_msg, advert->json_sz);

		SPIN_UNLOCK(&advert->lock, no_rev);
		if (FAILED(st) || FAILED(st = clock_sleep(SEND_DELAY_USEC)))
			break;
	}

	st2 = sock_close(advert->mcast_sock);
	if (!FAILED(st))
		st = st2;

	return (void*) (long) st;
}

status advert_create(advert_handle* padvert, const char* mcast_address,
					 unsigned short mcast_port, short mcast_ttl,
					 boolean mcast_loopback, const char* env)
{
	status st;
	if (!mcast_address)
		return error_invalid_arg("advert_create");

	*padvert = XMALLOC(struct advert);
	if (!*padvert)
		return NO_MEMORY;

	BZERO(*padvert);
	SPIN_CREATE(&(*padvert)->lock);

	(*padvert)->env = xstrdup(env);
	if (!(*padvert)->env)
		return NO_MEMORY;

	if (FAILED(st = sock_create(&(*padvert)->mcast_sock,
								SOCK_DGRAM, IPPROTO_UDP)) ||
		FAILED(st = sock_set_reuseaddr((*padvert)->mcast_sock, TRUE)) ||
		FAILED(st = sock_set_mcast_ttl((*padvert)->mcast_sock, mcast_ttl)) ||
		FAILED(st = sock_set_mcast_loopback((*padvert)->mcast_sock,
											mcast_loopback)) ||
		FAILED(st = sock_addr_create(&(*padvert)->mcast_addr,
									 mcast_address, mcast_port)) ||
		FAILED(st = sock_bind((*padvert)->mcast_sock, (*padvert)->mcast_addr)) ||
		FAILED(st = thread_create(&(*padvert)->mcast_thr,
								  mcast_func, *padvert))) {
		error_save_last();
		advert_destroy(padvert);
		error_restore_last();
	}

	return st;
}

status advert_destroy(advert_handle* padvert)
{
	status st = OK;
	struct notice* it;

	if (!padvert || !*padvert ||
		FAILED(st = thread_destroy(&(*padvert)->mcast_thr)) ||
		FAILED(st = sock_destroy(&(*padvert)->mcast_sock)) ||
		FAILED(st = sock_addr_destroy(&(*padvert)->mcast_addr)))
		return st;

	for (it = (*padvert)->notices; it; ) {
		struct notice* next = it->next;
		XFREE(it->json_desc);
		xfree(it);
		it = next;
	}

	XFREE((*padvert)->json_msg);
	XFREE((*padvert)->env);
	XFREE(*padvert);
	return st;
}

boolean advert_is_running(advert_handle advert)
{
	return advert->mcast_thr && thread_is_running(advert->mcast_thr);
}

status advert_stop(advert_handle advert)
{
	void* p;
	status st2, st = thread_stop(advert->mcast_thr, &p);
	if (!FAILED(st))
		st = (long) p;

	st2 = sock_close(advert->mcast_sock);
	if (!FAILED(st))
		st = st2;

	return st;
}

status advert_publish(advert_handle advert, sender_handle sender)
{
	struct notice* it;
	status st;

	if (!sender)
		return error_invalid_arg("advert_publish");

	for (it = advert->notices; it; it = it->next)
		if (it->sender == sender)
			return OK;

	it = XMALLOC(struct notice);
	if (!it)
		return NO_MEMORY;

	if (FAILED(st = make_json_notice(sender, &it->json_desc))) {
		XFREE(it);
		return st;
	}

	it->sender = sender;
	it->next = advert->notices;
	advert->notices = it;

	return make_json_map(advert);
}

status advert_retract(advert_handle advert, sender_handle sender)
{
	struct notice* it;
	struct notice** prev;

	if (!sender)
		return error_invalid_arg("advert_retract");

	for (prev = &advert->notices, it = advert->notices;
		 it;
		 prev = &it->next, it = it->next)
		if (it->sender == sender) {
			*prev = it->next;
			XFREE(it->json_desc);
			XFREE(it);

			return make_json_map(advert);
		}

	return NOT_FOUND;
}
