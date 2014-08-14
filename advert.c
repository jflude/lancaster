#include "advert.h"
#include "error.h"
#include "spin.h"
#include "thread.h"
#include "xalloc.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>

#define SEND_DELAY_USEC (3 * 1000 * 1000)

struct item_t
{
	sender_handle sender;
	char* json_desc;
	struct item_t* next;
};

struct advert_t
{
	sock_handle mcast_sock;
	thread_handle mcast_thr;
	char* json_msg;
	size_t json_sz;
	struct item_t* items;
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

static status make_json_map(advert_handle adv)
{
	char buf[8192], host[HOST_NAME_MAX];
	struct item_t* it;
	char* new_msg;

	strcpy(buf, "{ \"hostname\" : \"");

	if (gethostname(host, sizeof(host)) == -1) {
		error_errno("gethostname");
		return FAIL;
	}

	strcat(buf, escape_quotes(host));
	strcat(buf, "\", \"data\" : [ ");

	for (it = adv->items; it; it = it->next) {
		strcat(buf, it->json_desc);
		if (it->next)
			strcat(buf, ", ");
	}

	strcat(buf, " ] }");

	new_msg = xstrdup(buf);
	if (!new_msg)
		return NO_MEMORY;

	SPIN_WRITE_LOCK(&adv->lock, no_ver);

	xfree(adv->json_msg);
	adv->json_msg = new_msg;
	adv->json_sz = strlen(adv->json_msg) + 1;

	SPIN_UNLOCK(&adv->lock, no_ver);
	return OK;
}

static status make_json_item(sender_handle send, char** pjson)
{
	char buf[512];
	int n = sprintf(buf, "{ \"port\" : %d, \"description\" : \"%s\" }",
					sock_get_port(sender_get_listen_socket(send)),
					escape_quotes(storage_get_description(sender_get_storage(send))));
	if (n < 0) {
		error_errno("sprintf");
		return FAIL;
	}

	*pjson = xstrdup(buf);
	return *pjson ? OK : NO_MEMORY;
}

static void* mcast_func(thread_handle thr)
{
	advert_handle me = thread_get_param(thr);
	status st = OK, st2;

	while (!thread_is_stopping(thr)) {
		SPIN_WRITE_LOCK(&me->lock, no_ver);
		if (me->json_msg)
			st = sock_sendto(me->mcast_sock, me->json_msg, me->json_sz);

		SPIN_UNLOCK(&me->lock, no_ver);
		if (FAILED(st) || FAILED(st = clock_sleep(SEND_DELAY_USEC)))
			break;
	}

	st2 = sock_close(me->mcast_sock);
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

	*padvert = XMALLOC(struct advert_t);
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
	struct item_t* it;
	if (!padvert || !*padvert)
		return;

	error_save_last();

	thread_destroy(&(*padvert)->mcast_thr);
	sock_destroy(&(*padvert)->mcast_sock);

	error_restore_last();

	for (it = (*padvert)->items; it; ) {
		struct item_t* next = it->next;
		xfree(it->json_desc);
		xfree(it);
		it = next;
	}

	xfree((*padvert)->json_msg);
	xfree(*padvert);
	*padvert = NULL;
}

boolean advert_is_running(advert_handle adv)
{
	return adv->mcast_thr && thread_is_running(adv->mcast_thr);
}

status advert_stop(advert_handle adv)
{
	void* p;
	status st2, st = thread_stop(adv->mcast_thr, &p);
	if (!FAILED(st))
		st = (long) p;

	st2 = sock_close(adv->mcast_sock);
	if (!FAILED(st))
		st = st2;

	return st;
}

status advert_publish(advert_handle adv, sender_handle send)
{
	struct item_t* it;
	status st;

	if (!send) {
		error_invalid_arg("advert_publish");
		return FAIL;
	}

	for (it = adv->items; it; it = it->next)
		if (it->sender == send)
			return FAIL;

	it = XMALLOC(struct item_t);
	if (!it)
		return NO_MEMORY;

	if (FAILED(st = make_json_item(send, &it->json_desc))) {
		xfree(it);
		return st;
	}

	it->sender = send;
	it->next = adv->items;
	adv->items = it;

	return make_json_map(adv);
}

status advert_retract(advert_handle adv, sender_handle send)
{
	struct item_t* it;
	struct item_t** prev;

	if (!send) {
		error_invalid_arg("advert_retract");
		return FAIL;
	}

	for (prev = &adv->items, it = adv->items; it; prev = &it->next, it = it->next)
		if (it->sender == send) {
			*prev = it->next;
			xfree(it->json_desc);
			xfree(it);
			return make_json_map(adv);
		}

	return FAIL;
}
