/* portable IPv4 sockets */

#ifndef SOCK_H
#define SOCK_H

#include "status.h"
#include <stddef.h>

#define MTU_BYTES 1500

#ifdef __cplusplus
extern "C" {
#endif

struct sock_t;
typedef struct sock_t* sock_handle;

status sock_create(sock_handle* psock, int type, const char* address, int port);
void sock_destroy(sock_handle* psock);

int sock_get_descriptor(sock_handle sock);
void* sock_get_property(sock_handle sock);
void sock_set_property(sock_handle sock, void* prop);

status sock_nonblock(sock_handle sock);
status sock_shutdown(sock_handle sock, int how);
status sock_close(sock_handle sock);

status sock_mcast_bind(sock_handle sock);
status sock_mcast_set_ttl(sock_handle sock, int ttl);

status sock_listen(sock_handle sock, int backlog);
status sock_accept(sock_handle sock, sock_handle* new_h);
status sock_connect(sock_handle sock);
status sock_get_dest_address(sock_handle sock, char* dest, size_t sz);

status sock_write(sock_handle sock, const void* data, size_t sz);
status sock_read(sock_handle sock, void* data, size_t sz);
status sock_sendto(sock_handle sock, const void* data, size_t sz);
status sock_recvfrom(sock_handle sock, void* data, size_t sz);

#ifdef __cplusplus
}
#endif

#endif
