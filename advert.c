#include "advert.h"
#include "error.h"
#include "spin.h"
#include "thread.h"
#include "xalloc.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>

#define SEND_DELAY_USEC (3 * 1000 * 1000)

struct notice
{
	sender_handle sender;
	char* json_desc;
	struct notice* next;
};

struct advert
{
	sock_handle mcast_sock;
	thread_handle mcast_thr;
	char* json_msg;
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
	char buf[8192], host[HOST_NAME_MAX];
	struct notice* it;
	char* new_msg;

	strcpy(buf, "{ \"hostname\" : \"");

	if (gethostname(host, sizeof(host)) == -1) {
		error_errno("gethostname");
		return FAIL;
	}

	strcat(buf, escape_quotes(host));
	strcat(buf, "\", \"data\" : [ ");

	for (it = advert->notices; it; it = it->next) {
		strcat(buf, it->json_desc);
		if (it->next)
			strcat(buf, ", ");
	}

	strcat(buf, " ] }");

	new_msg = xstrdup(buf);
	if (!new_msg)
		return NO_MEMORY;

	SPIN_WRITE_LOCK(&advert->lock, no_ver);

	xfree(advert->json_msg);
	advert->json_msg = new_msg;
	advert->json_sz = strlen(advert->json_msg) + 1;

	SPIN_UNLOCK(&advert->lock, no_ver);
	return OK;
}

static status make_json_notice(sender_handle sender, char** pjson)
{
	char buf[512];
	int n = sprintf(buf, "{ \"port\" : %d, \"description\" : \"%s\" }",
					sender_get_listen_port(sender), escape_quotes(storage_get_description(sender_get_storage(sender))));
	if (n < 0) {
		error_errno("sprintf");
		return FAIL;
	}

	*pjson = xstrdup(buf);
	return *pjson ? OK : NO_MEMORY;
}

static void* mcast_func(thread_handle thr)
{
	advert_handle advert = thread_get_param(thr);
	status st = OK, st2;

	while (!thread_is_stopping(thr)) {
		SPIN_WRITE_LOCK(&advert->lock, no_ver);
		if (advert->json_msg)
			st = sock_sendto(advert->mcast_sock, advert->json_msg, advert->json_sz);

		SPIN_UNLOCK(&advert->lock, no_ver);
		if (FAILED(st) || FAILED(st = clock_sleep(SEND_DELAY_USEC)))
			break;
	}

	st2 = sock_close(advert->mcast_sock);
	if (!FAILED(st))
		st = st2;

	return (void*) (long) st;
}

status advert_create(advert_handle* padvert, const char* mcast_addr, int mcast_port, int mcast_ttl)
{
	status st;
	if (!mcast_addr || mcast_port < 0) {
		error_invalid_arg("advert_create");
		return FAIL;
	}

	*padvert = XMALLOC(struct advert);
	if (!*padvert)
		return NO_MEMORY;

	BZERO(*padvert);
	SPIN_CREATE(&(*padvert)->lock);

	if (FAILED(st = sock_create(&(*padvert)->mcast_sock, SOCK_DGRAM, mcast_addr, mcast_port)) ||
		FAILED(st = sock_set_reuseaddr((*padvert)->mcast_sock, 1)) ||
		FAILED(st = sock_mcast_bind((*padvert)->mcast_sock)) ||
		FAILED(st = sock_set_mcast_ttl((*padvert)->mcast_sock, mcast_ttl)) ||
		FAILED(st = thread_create(&(*padvert)->mcast_thr, mcast_func, *padvert))) {
		advert_destroy(padvert);
		return st;
	}

	return OK;
}

void advert_destroy(advert_handle* padvert)
{
	struct notice* it;
	if (!padvert || !*padvert)
		return;

	error_save_last();

	thread_destroy(&(*padvert)->mcast_thr);
	sock_destroy(&(*padvert)->mcast_sock);

	error_restore_last();

	for (it = (*padvert)->notices; it; ) {
		struct notice* next = it->next;
		xfree(it->json_desc);
		xfree(it);
		it = next;
	}

	xfree((*padvert)->json_msg);
	xfree(*padvert);
	*padvert = NULL;
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

	if (!sender) {
		error_invalid_arg("advert_publish");
		return FAIL;
	}

	for (it = advert->notices; it; it = it->next)
		if (it->sender == sender)
			return FAIL;

	it = XMALLOC(struct notice);
	if (!it)
		return NO_MEMORY;

	if (FAILED(st = make_json_notice(sender, &it->json_desc))) {
		xfree(it);
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

	if (!sender) {
		error_invalid_arg("advert_retract");
		return FAIL;
	}

	for (prev = &advert->notices, it = advert->notices; it; prev = &it->next, it = it->next)
		if (it->sender == sender) {
			*prev = it->next;
			xfree(it->json_desc);
			xfree(it);
			return make_json_map(advert);
		}

	return FAIL;
}
