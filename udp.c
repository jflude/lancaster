/* 
 * File:   udp.c
 * Author: jrobertson
 *
 * Created on September 24, 2014, 2:25 PM
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "udp.h"
#include "error.h"


status open_udp_sock_conn(udp_conn_handle a_udp_conn, const char* a_url)
{
    status st = OK;
    char ip[64];
    int port = 0;
    /* tokenize string */
    char* colon = strchr(a_url, ':');
    if (colon == NULL) {
        return error_msg("Bad UDP_STAT_URL specified: %s", BAD_UDP_STATS_URL, 
            a_url);
    }
    /* get ip and port from url */
    memcpy(ip, a_url, colon-a_url);
    ip[colon-a_url+1] = 0;
    port = atoi(++colon);
    /* create socket */
    if (FAILED(st = sock_create(&a_udp_conn->sock_fd_, SOCK_DGRAM, IPPROTO_UDP)) ||
        FAILED(st = sock_addr_create(&a_udp_conn->server_sock_addr_, ip, port))) {
        return st;
    }

    return st;
}

status close_udp_sock_conn(udp_conn_handle a_udp_conn)
{
    status st = OK;
    if (FAILED(st = sock_destroy(&a_udp_conn->sock_fd_)) ||
        FAILED(st = sock_addr_destroy(&a_udp_conn->server_sock_addr_)))
        return st;
    
    return st;
}

/*
int send_udp_msg(udp_conn_handle a_udp_conn, const char* a_buffer, size_t a_buffer_size)
{
    status st = OK;
    st = sendto(a_udp_conn->sock_fd_->fd, a_buffer, a_buffer_size, 0, 
        (struct sockaddr*)a_udp_conn->server_sock_addr_->sa, 
        sizeof(struct sockaddr));
     return st;
}
*/