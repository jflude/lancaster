/* 
 * File:   udp.c
 * Author: jrobertson
 *
 * Created on September 24, 2014, 2:25 PM
 */

#include "udp.h"
#include "error.h"
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>

status open_udp_sock_conn(udp_conn_handle a_udp_conn, const char *a_url)
{
    status st;
    char ip[64];
    int port;

    char *colon = strchr(a_url, ':');
    if (!colon)
        return error_msg("open_udp_sock_conn: error: invalid address: \"%s\"",
						 BAD_UDP_STATS_URL, a_url);

	strncpy(ip, a_url, colon - a_url);
	ip[colon - a_url] = '\0';

	port = atoi(colon + 1);

    if (!FAILED(st = sock_create(&a_udp_conn->sock_fd_,
								 SOCK_DGRAM, IPPROTO_UDP)))
        st = sock_addr_create(&a_udp_conn->server_sock_addr_, ip, port);

    return st;
}

status close_udp_sock_conn(udp_conn_handle a_udp_conn)
{
    status st;
    if (!FAILED(st = sock_destroy(&a_udp_conn->sock_fd_)))
        st = sock_addr_destroy(&a_udp_conn->server_sock_addr_);
    
    return st;
}

#if 0
int send_udp_msg(udp_conn_handle a_udp_conn, const char *a_buffer,
				 size_t a_buffer_size)
{
    status st = sendto(a_udp_conn->sock_fd_->fd, a_buffer, a_buffer_size, 0, 
					   (struct sockaddr *)a_udp_conn->server_sock_addr_->sa,
					   sizeof(struct sockaddr));
	return st;
}
#endif
