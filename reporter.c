#include <string.h>
#include <netinet/in.h>
#include "error.h"
#include "reporter.h"
#include "sock.h"
#include "xalloc.h"

struct reporter {
	sock_handle udp_sock;
	sock_addr_handle udp_addr;
};

status reporter_create(reporter_handle *prep, const char *udp_address,
					   unsigned short udp_port)
{
	status st;
	if (!prep || !udp_address)
		return error_invalid_arg("reporter_create");

	*prep = XMALLOC(struct reporter);
	if (!*prep)
		return NO_MEMORY;

	BZERO(*prep);

	if (FAILED(st = sock_create(&(*prep)->udp_sock,
								SOCK_DGRAM, IPPROTO_UDP)) ||
		FAILED(st = sock_addr_create(&(*prep)->udp_addr,
									 udp_address, udp_port))) {
		error_save_last();
		reporter_destroy(prep);
		error_restore_last();
	}

	return st;
}

status reporter_destroy(reporter_handle *prep)
{
	status st = OK;
	if (!prep || !*prep ||
		FAILED(st = sock_addr_destroy(&(*prep)->udp_addr)) ||
		FAILED(st = sock_destroy(&(*prep)->udp_sock)))
		return st;

	XFREE(*prep);
	return st;
}

status reporter_send(reporter_handle rep, const char *msg)
{
	if (!msg)
		return error_invalid_arg("reporter_send");

	return sock_sendto(rep->udp_sock, rep->udp_addr, msg, strlen(msg));
}
