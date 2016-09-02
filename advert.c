#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "advert.h"
#include "error.h"
#include "socket.h"
#include "spin.h"
#include "thread.h"
#include "xalloc.h"

struct notice {
	sender_handle sender;
	char *json_desc;
	struct notice *next;
};

struct advert {
	sock_handle mcast_sock;
	sock_addr_handle sendto_addr;
	thread_handle mcast_thr;
	char *json_msg;
	char *env;
	size_t json_sz;
	microsec tx_period_usec;
	struct notice *notices;
	volatile spin_lock lock;
};

static const char *escape_quotes(const char *in)
{
	static char buf[512];
	char *out = buf;

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
	struct notice *n;
	char *new_msg;

	status st;
	if (FAILED(st = sock_get_hostname(buf, sizeof(buf))))
		return st;

	strcpy(hostname, escape_quotes(buf));

	sprintf(buf, "{\"hostname\":\"%s\", \"env\":\"%s\", "
			"\"version\":\"%d.%d\", \"data\":[",
			hostname, escape_quotes(advert->env),
			LANCASTER_WIRE_MAJOR_VERSION, LANCASTER_WIRE_MINOR_VERSION);

	for (n = advert->notices; n; n = n->next) {
		strcat(buf, n->json_desc);
		if (n->next)
			strcat(buf, ", ");
	}

	strcat(buf, "]}");

	new_msg = xstrdup(buf);
	if (!new_msg)
		return NO_MEMORY;

	if (FAILED(st = spin_write_lock(&advert->lock, NULL)))
		return st;

	xfree(advert->json_msg);
	advert->json_msg = new_msg;
	advert->json_sz = strlen(advert->json_msg);

	spin_unlock(&advert->lock, 0);
	return OK;
}

static status make_json_notice(sender_handle sender, char **pjson)
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

static void *mcast_func(thread_handle thr)
{
	advert_handle advert = thread_get_param(thr);
	status st = OK, st2;

	while (!thread_is_stopping(thr)) {
		if (FAILED(st = clock_sleep(advert->tx_period_usec)) ||
			FAILED(st = spin_write_lock(&advert->lock, NULL)))
			break;

		if (advert->json_msg)
			st = sock_sendto(advert->mcast_sock, advert->sendto_addr,
							 advert->json_msg, advert->json_sz);

		spin_unlock(&advert->lock, 0);
		if (FAILED(st))
			break;
	}

	st2 = sock_close(advert->mcast_sock);
	if (!FAILED(st))
		st = st2;

	return (void *)(long)st;
}

static status init(advert_handle *padvert, const char *mcast_address,
				   unsigned short mcast_port, const char *mcast_interface,
				   short mcast_ttl, boolean mcast_loopback, const char *env,
				   microsec tx_period_usec)
{
	status st;
	BZERO(*padvert);
	spin_create(&(*padvert)->lock);

	(*padvert)->env = xstrdup(env ? env : "");
	if (!(*padvert)->env)
		return NO_MEMORY;

	(*padvert)->tx_period_usec = tx_period_usec;

	if (FAILED(st = sock_create(&(*padvert)->mcast_sock,
								SOCK_DGRAM, IPPROTO_UDP)))
		return st;

	if (mcast_interface) {
		sock_addr_handle if_addr;
		if (FAILED(st = sock_addr_create(&if_addr, NULL, 0)) ||
			FAILED(st = sock_get_interface_address((*padvert)->mcast_sock,
												   mcast_interface,
												   if_addr)) ||
			FAILED(st = sock_set_mcast_interface((*padvert)->mcast_sock,
												 if_addr)) ||
			FAILED(st = sock_addr_destroy(&if_addr)))
			return st;
	}

	(void)(FAILED(st = sock_set_reuseaddr((*padvert)->mcast_sock, TRUE)) ||
		   FAILED(st = sock_set_mcast_ttl((*padvert)->mcast_sock, mcast_ttl)) ||
		   FAILED(st = sock_set_mcast_loopback((*padvert)->mcast_sock,
											   mcast_loopback)) ||
		   FAILED(st = sock_addr_create(&(*padvert)->sendto_addr,
										mcast_address, mcast_port)) ||
		   FAILED(st = thread_create(&(*padvert)->mcast_thr,
									 mcast_func, *padvert)));
	return st;
}

status advert_create(advert_handle *padvert, const char *mcast_address,
					 unsigned short mcast_port, const char *mcast_interface,
					 short mcast_ttl, boolean mcast_loopback, const char *env,
					 microsec tx_period_usec)
{
	status st;
	if (!padvert || !mcast_address || tx_period_usec <= 0)
		return error_invalid_arg("advert_create");

	*padvert = XMALLOC(struct advert);
	if (!*padvert)
		return NO_MEMORY;

	if (FAILED(st = init(padvert, mcast_address, mcast_port, mcast_interface,
						 mcast_ttl, mcast_loopback, env, tx_period_usec))) {
		error_save_last();
		advert_destroy(padvert);
		error_restore_last();
	}

	return st;
}

status advert_destroy(advert_handle *padvert)
{
	status st = OK;
	struct notice *n;

	if (!padvert || !*padvert ||
		FAILED(st = thread_destroy(&(*padvert)->mcast_thr)) ||
		FAILED(st = sock_destroy(&(*padvert)->mcast_sock)) ||
		FAILED(st = sock_addr_destroy(&(*padvert)->sendto_addr)))
		return st;

	for (n = (*padvert)->notices; n; ) {
		struct notice *next = n->next;
		xfree(n->json_desc);
		xfree(n);
		n = next;
	}

	xfree((*padvert)->json_msg);
	xfree((*padvert)->env);
	XFREE(*padvert);
	return st;
}

boolean advert_is_running(advert_handle advert)
{
	return advert->mcast_thr && thread_is_running(advert->mcast_thr);
}

status advert_stop(advert_handle advert)
{
	void *p;
	status st2, st = thread_stop(advert->mcast_thr, &p);
	if (!FAILED(st))
		st = (long)p;

	st2 = sock_close(advert->mcast_sock);
	if (!FAILED(st))
		st = st2;

	return st;
}

status advert_publish(advert_handle advert, sender_handle sender)
{
	struct notice *n;
	status st;

	if (!sender)
		return error_invalid_arg("advert_publish");

	for (n = advert->notices; n; n = n->next)
		if (n->sender == sender)
			return OK;

	n = XMALLOC(struct notice);
	if (!n)
		return NO_MEMORY;

	if (FAILED(st = make_json_notice(sender, &n->json_desc))) {
		xfree(n);
		return st;
	}

	n->sender = sender;
	n->next = advert->notices;
	advert->notices = n;

	return make_json_map(advert);
}

status advert_retract(advert_handle advert, sender_handle sender)
{
	struct notice *n;
	struct notice **prev;

	if (!sender)
		return error_invalid_arg("advert_retract");

	for (prev = &advert->notices, n = advert->notices;
		 n;
		 prev = &n->next, n = n->next)
		if (n->sender == sender) {
			*prev = n->next;
			xfree(n->json_desc);
			xfree(n);

			return make_json_map(advert);
		}

	return NOT_FOUND;
}
