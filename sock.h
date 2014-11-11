/* portable IPv4 sockets */

#ifndef SOCK_H
#define SOCK_H

#include <stddef.h>
#include "status.h"

#define DEFAULT_MTU 1500
#define IP_OVERHEAD 20
#define UDP_OVERHEAD 8

#ifdef __cplusplus
extern "C" {
#endif

struct sock_addr;
typedef struct sock_addr *sock_addr_handle;

status sock_addr_create(sock_addr_handle *paddr, const char *address,
						unsigned short port);
status sock_addr_destroy(sock_addr_handle *paddr);

unsigned long sock_addr_get_ip(sock_addr_handle addr);
unsigned short sock_addr_get_port(sock_addr_handle addr);
status sock_addr_get_text(sock_addr_handle addr, char *text, size_t
						  text_sz, boolean with_port);
status sock_addr_split(const char *addr_and_port, char *paddr,
					   size_t addr_sz, unsigned short *pport);

boolean sock_addr_is_equal(sock_addr_handle lhs, sock_addr_handle rhs);

void sock_addr_set_none(sock_addr_handle addr);
void sock_addr_copy(sock_addr_handle dest, sock_addr_handle src);

struct sock;
typedef struct sock *sock_handle;

status sock_create(sock_handle *psock, int type, int protocol);
status sock_destroy(sock_handle *psock);

status sock_get_hostname(char *name, size_t name_sz);

void *sock_get_property_ref(sock_handle sock);
void sock_set_property_ref(sock_handle sock, void *prop);

int sock_get_descriptor(sock_handle sock);
status sock_get_local_address(sock_handle sock, sock_addr_handle addr);
status sock_get_remote_address(sock_handle sock, sock_addr_handle addr);
status sock_get_interface_address(sock_handle sock, const char *device,
								  sock_addr_handle addr);
status sock_get_device(const char *dest_address, char *pdevice,
					   size_t device_sz);
status sock_get_mtu(sock_handle sock, const char *device, size_t *pmtu);

status sock_set_nonblock(sock_handle sock);
status sock_set_reuseaddr(sock_handle sock, boolean reuse);
status sock_set_rx_buf(sock_handle sock, size_t buf_sz);
status sock_set_mcast_ttl(sock_handle sock, short ttl);
status sock_set_mcast_loopback(sock_handle sock, boolean allow_loop);
status sock_set_mcast_interface(sock_handle sock, sock_addr_handle addr);

status sock_bind(sock_handle sock, sock_addr_handle addr);
status sock_listen(sock_handle sock, int backlog);
status sock_accept(sock_handle sock, sock_handle *new_h);
status sock_connect(sock_handle sock, sock_addr_handle addr);
status sock_mcast_add(sock_handle sock, sock_addr_handle multi_addr,
					  sock_addr_handle iface_addr);
status sock_mcast_drop(sock_handle sock, sock_addr_handle multi_addr,
					   sock_addr_handle iface_addr);

status sock_write(sock_handle sock, const void *data, size_t data_sz);
status sock_read(sock_handle sock, void *data, size_t data_sz);
status sock_sendto(sock_handle sock, sock_addr_handle addr,
				   const void *data, size_t data_sz);
status sock_recvfrom(sock_handle sock, sock_addr_handle addr,
					 void *data, size_t data_sz);

status sock_shutdown(sock_handle sock, int how);
status sock_close(sock_handle sock);

#ifdef __cplusplus
}
#endif

#endif
