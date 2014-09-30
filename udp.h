/* 
 * File:   udp.h
 * Author: jrobertson
 *
 * Created on September 24, 2014, 2:25 PM
 */

#ifndef _UDP_H_
#define	_UDP_H_

#include "sock.h"
#include "status.h"

#ifdef	__cplusplus
extern "C" {
#endif
    
struct udp_conn_info {
    sock_handle sock_fd_;
    sock_addr_handle server_sock_addr_;
};

typedef struct udp_conn_info* udp_conn_handle;

status open_udp_sock_conn(udp_conn_handle a_udp_conn, const char* a_url);
status close_udp_sock_conn(udp_conn_handle a_udp_conn);

#ifdef	__cplusplus
}
#endif

#endif	/* _UDP_H_ */
