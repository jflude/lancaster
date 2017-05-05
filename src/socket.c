/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "a2i.h"
#include "error.h"
#include "socket.h"
#include "xalloc.h"

#if defined(SIOCRIPMTU) && defined(SIOCSIPMTU)
#define SIOCGIFMTU SIOCRIPMTU
#define SIOCSIFMTU SIOCSIPMTU
#endif

#if !defined(ifr_mtu) && defined(ifr_metric)
#define ifr_mtu ifr_metric
#endif

struct sock {
    int fd;
    void *prop_ref;
};

struct sock_addr {
    struct sockaddr_in sa;
};

status sock_addr_create(sock_addr_handle * paddr, const char *address,
			unsigned short port)
{
    if (!paddr)
	return error_invalid_arg("sock_addr_create");

    *paddr = XMALLOC(struct sock_addr);
    if (!*paddr)
	return NO_MEMORY;

    BZERO(*paddr);

    (*paddr)->sa.sin_family = AF_INET;
    (*paddr)->sa.sin_port = htons(port);

    if (!address)
	(*paddr)->sa.sin_addr.s_addr = htonl(INADDR_ANY);
    else if (!inet_aton(address, &(*paddr)->sa.sin_addr)) {
	sock_addr_destroy(paddr);
	return error_msg(INVALID_ADDRESS,
			 "inet_aton: invalid address: \"%s:%d\"",
			 address, (int) port);
    }

    return OK;
}

status sock_addr_destroy(sock_addr_handle * paddr)
{
    if (paddr && *paddr)
	XFREE(*paddr);

    return OK;
}

unsigned long sock_addr_get_ip(sock_addr_handle addr)
{
    return ntohl(addr->sa.sin_addr.s_addr);
}

unsigned short sock_addr_get_port(sock_addr_handle addr)
{
    return ntohs(addr->sa.sin_port);
}

status sock_addr_get_text(sock_addr_handle addr, char *text,
			  size_t text_sz, boolean with_port)
{
    char a_buf[256], p_buf[16];
    if (!text || text_sz < INET_ADDRSTRLEN)
	return error_invalid_arg("sock_addr_get_text");

    if (!inet_ntop(addr->sa.sin_family, &addr->sa.sin_addr,
		   a_buf, sizeof(a_buf)))
	return error_errno("inet_ntop");

    if (!with_port)
	p_buf[0] = '\0';
    else if (sprintf(p_buf, ":%d", (int) ntohs(addr->sa.sin_port)) < 0)
	return error_errno("sock_addr_get_text");

    if (strlen(a_buf) + strlen(p_buf) >= text_sz)
	return error_msg(BUFFER_TOO_SMALL,
			 "sock_addr_get_text: buffer too small");

    strcpy(text, a_buf);
    strcat(text, p_buf);
    return OK;
}

status sock_addr_split(const char *addr_and_port, char *paddr,
		       size_t addr_sz, unsigned short *pport)
{
    size_t sz;
    const char *colon;
    if (!addr_and_port || !paddr || addr_sz == 0 || !pport)
	return error_invalid_arg("sock_addr_split");

    colon = strchr(addr_and_port, ':');
    if (!colon)
	return error_msg(INVALID_ADDRESS,
			 "sock_addr_split: invalid address: \"%s\"",
			 addr_and_port);

    sz = colon - addr_and_port;
    if (sz >= addr_sz)
	return error_msg(BUFFER_TOO_SMALL,
			 "sock_addr_split: buffer too small");

    strncpy(paddr, addr_and_port, sz);
    paddr[sz] = '\0';

    return a2i(colon + 1, "%hu", pport);
}

boolean sock_addr_is_equal(sock_addr_handle lhs, sock_addr_handle rhs)
{
    return lhs && rhs &&
	lhs->sa.sin_addr.s_addr == rhs->sa.sin_addr.s_addr &&
	lhs->sa.sin_port == rhs->sa.sin_port;
}

void sock_addr_set_none(sock_addr_handle addr)
{
    addr->sa.sin_addr.s_addr = htonl(INADDR_NONE);
    addr->sa.sin_port = 0;
}

void sock_addr_copy(sock_addr_handle dest, sock_addr_handle src)
{
    dest->sa = src->sa;
}

status sock_create(sock_handle * psock, int type, int protocol)
{
    if (!psock)
	return error_invalid_arg("sock_create");

    *psock = XMALLOC(struct sock);
    if (!*psock)
	return NO_MEMORY;

    BZERO(*psock);

    (*psock)->fd = socket(AF_INET, type, protocol);
    if ((*psock)->fd == -1) {
	error_save_last();
	sock_destroy(psock);
	error_restore_last();
	return error_errno("socket");
    }

    return OK;
}

status sock_destroy(sock_handle * psock)
{
    status st = OK;
    if (!psock || !*psock)
	return st;

    st = sock_close(*psock);

    XFREE(*psock);
    return st;
}

status sock_get_hostname(char *name, size_t name_sz)
{
    if (!name || name_sz == 0)
	return error_invalid_arg("sock_get_hostname");

    if (gethostname(name, name_sz) == -1)
	return error_errno("sock_get_hostname");

    name[name_sz - 1] = '\0';
    return OK;
}

void *sock_get_property_ref(sock_handle sock)
{
    return sock->prop_ref;
}

void sock_set_property_ref(sock_handle sock, void *prop)
{
    sock->prop_ref = prop;
}

int sock_get_descriptor(sock_handle sock)
{
    return sock->fd;
}

status sock_get_local_address(sock_handle sock, sock_addr_handle addr)
{
    socklen_t len;
    if (!addr)
	return error_invalid_arg("sock_get_local_address");

    len = sizeof(addr->sa);
    if (getsockname(sock->fd, (struct sockaddr *) &addr->sa, &len) == -1)
	return error_errno("getsockname");

    return OK;
}

status sock_get_remote_address(sock_handle sock, sock_addr_handle addr)
{
    socklen_t len;
    if (!addr)
	return error_invalid_arg("sock_get_remote_address");

    len = sizeof(addr->sa);
    if (getpeername(sock->fd, (struct sockaddr *) &addr->sa, &len) == -1)
	return error_errno("getpeername");

    return OK;
}

status sock_get_interface_address(sock_handle sock, const char *device,
				  sock_addr_handle addr)
{
    struct ifreq ifr;
    if (!device)
	return error_invalid_arg("sock_get_interface_address");

    BZERO(&ifr);
    strncpy(ifr.ifr_name, device, IFNAMSIZ);

    if (ioctl(sock->fd, (int) SIOCGIFADDR, &ifr) == -1)
	return error_errno("ioctl");

    addr->sa.sin_addr = ((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr;
    return OK;
}

status sock_get_mtu(sock_handle sock, const char *device, size_t * pmtu)
{
    struct ifreq ifr;
    if (!device || !pmtu)
	return error_invalid_arg("sock_get_mtu");

    BZERO(&ifr);
    strncpy(ifr.ifr_name, device, IFNAMSIZ);

    if (ioctl(sock->fd, (int) SIOCGIFMTU, &ifr) == -1)
	return error_errno("ioctl");

    *pmtu = ifr.ifr_mtu;
    return OK;
}

status sock_set_nonblock(sock_handle sock)
{
    int flags;
#ifdef O_NONBLOCK
    flags = fcntl(sock->fd, F_GETFL, 0);
    if (flags == -1)
	return error_errno("fcntl");

    if (fcntl(sock->fd, F_SETFL, flags | O_NONBLOCK) == -1)
	return error_errno("fcntl");
#else
    flags = 1;
    if (ioctl(sock->fd, (int) FIOBIO, &flags) == -1)
	return error_errno("ioctl");
#endif
    return OK;
}

status sock_set_reuseaddr(sock_handle sock, boolean reuse)
{
    int val = !!reuse;
    if (setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR,
		   &val, sizeof(val)) == -1)
	return error_errno("setsockopt");

    return OK;
}

status sock_set_rx_buf(sock_handle sock, size_t buf_sz)
{
    int val = buf_sz;
    if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val)) == -1)
	return error_errno("setsockopt");

    return OK;
}

status sock_set_tx_buf(sock_handle sock, size_t buf_sz)
{
    int val = buf_sz;
    if (setsockopt(sock->fd, SOL_SOCKET, SO_SNDBUF, &val, sizeof(val)) == -1)
	return error_errno("setsockopt");

    return OK;
}

status sock_set_tcp_nodelay(sock_handle sock, boolean disable_delay)
{
    int val = !!disable_delay;
    if (setsockopt(sock->fd, IPPROTO_TCP, TCP_NODELAY,
		   &val, sizeof(val)) == -1)
	 return error_errno("setsockopt");

    return OK;
}

status sock_set_mcast_ttl(sock_handle sock, short ttl)
{
    unsigned char val = ttl;
    if (setsockopt(sock->fd, IPPROTO_IP, IP_MULTICAST_TTL,
		   &val, sizeof(val)) == -1)
	return error_errno("setsockopt");

    return OK;
}

status sock_set_mcast_loopback(sock_handle sock, boolean allow_loop)
{
    if (setsockopt(sock->fd, IPPROTO_IP, IP_MULTICAST_LOOP,
		   &allow_loop, sizeof(allow_loop)) == -1)
	return error_errno("setsockopt");

    return OK;
}

status sock_set_mcast_interface(sock_handle sock, sock_addr_handle addr)
{
    if (!addr)
	return error_invalid_arg("sock_set_mcast_interface");

    if (setsockopt(sock->fd, IPPROTO_IP, IP_MULTICAST_IF,
		   &addr->sa.sin_addr, sizeof(addr->sa.sin_addr)) == -1)
	return error_errno("setsockopt");

    return OK;
}

status sock_bind(sock_handle sock, sock_addr_handle addr)
{
    if (!addr)
	return error_invalid_arg("sock_bind");

    if (bind(sock->fd, (struct sockaddr *) &addr->sa, sizeof(addr->sa)) == -1)
	return error_errno("bind");

    return OK;
}

status sock_listen(sock_handle sock, int backlog)
{
    if (listen(sock->fd, backlog) == -1)
	return error_errno("listen");

    return OK;
}

status sock_accept(sock_handle sock, sock_handle * new_sock)
{
    struct sock accpt;
    if (!new_sock)
	return error_invalid_arg("sock_accept");

    BZERO(&accpt);

    accpt.fd = accept(sock->fd, NULL, NULL);
    if (accpt.fd == -1) {
#ifdef EAGAIN
	if (errno == EAGAIN)
	    return BLOCKED;
#endif
#ifdef EWOULDBLOCK
	if (errno == EWOULDBLOCK)
	    return BLOCKED;
#endif
	return error_eintr("accept");
    }

    *new_sock = XMALLOC(struct sock);
    if (!*new_sock)
	return close(accpt.fd) == -1 ? error_eintr("close") : NO_MEMORY;

    **new_sock = accpt;
    return OK;
}

status sock_connect(sock_handle sock, sock_addr_handle addr)
{
    if (!addr)
	return error_invalid_arg("sock_connect");

    if (connect(sock->fd, (const struct sockaddr *) &addr->sa,
		sizeof(addr->sa)) == -1)
	return error_errno("connect");

    return OK;
}

status sock_mcast_add(sock_handle sock, sock_addr_handle multi_addr,
		      sock_addr_handle iface_addr)
{
    struct ip_mreq mreq;
    if (!multi_addr || !iface_addr)
	return error_invalid_arg("sock_mcast_add");

    mreq.imr_multiaddr = multi_addr->sa.sin_addr;
    mreq.imr_interface = iface_addr->sa.sin_addr;

    if (setsockopt(sock->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		   &mreq, sizeof(mreq)) == -1)
	return error_errno("setsockopt");

    return OK;
}

status sock_mcast_drop(sock_handle sock, sock_addr_handle multi_addr,
		       sock_addr_handle iface_addr)
{
    struct ip_mreq mreq;
    if (!multi_addr || !iface_addr)
	return error_invalid_arg("sock_mcast_drop");

    mreq.imr_multiaddr.s_addr = multi_addr->sa.sin_addr.s_addr;
    mreq.imr_interface.s_addr = iface_addr->sa.sin_addr.s_addr;

    if (setsockopt(sock->fd, IPPROTO_IP, IP_DROP_MEMBERSHIP,
		   &mreq, sizeof(mreq)) == -1)
	return error_errno("setsockopt");

    return OK;
}

status sock_write(sock_handle sock, const void *data, size_t data_sz)
{
    ssize_t count;
    if (!data || data_sz == 0)
	return error_invalid_arg("sock_write");

    count = write(sock->fd, data, data_sz);
    if (count == -1) {
#ifdef EAGAIN
	if (errno == EAGAIN)
	    return BLOCKED;
#endif
#ifdef EWOULDBLOCK
	if (errno == EWOULDBLOCK)
	    return BLOCKED;
#endif
	if (errno == EPIPE || errno == ECONNRESET)
	    return error_eof("write");

	return error_eintr("write");
    }

    return count;
}

status sock_read(sock_handle sock, void *data, size_t data_sz)
{
    ssize_t count;
    if (!data || data_sz == 0)
	return error_invalid_arg("sock_read");

    count = read(sock->fd, data, data_sz);
    if (count == -1) {
#ifdef EAGAIN
	if (errno == EAGAIN)
	    return BLOCKED;
#endif
#ifdef EWOULDBLOCK
	if (errno == EWOULDBLOCK)
	    return BLOCKED;
#endif
	if (errno == ECONNRESET)
	    return error_eof("read");

	return error_eintr("read");
    }

    if (count == 0)
	return error_eof("read");

    return count;
}

status sock_sendto(sock_handle sock, sock_addr_handle addr,
		   const void *data, size_t data_sz)
{
    ssize_t count;
    if (!addr || !data || data_sz == 0)
	return error_invalid_arg("sock_sendto");

    count = sendto(sock->fd, data, data_sz, 0,
		   (const struct sockaddr *) &addr->sa, sizeof(addr->sa));
    if (count == -1) {
#ifdef EAGAIN
	if (errno == EAGAIN)
	    return BLOCKED;
#endif
#ifdef EWOULDBLOCK
	if (errno == EWOULDBLOCK)
	    return BLOCKED;
#endif
	if (errno == EPIPE || errno == ECONNRESET)
	    return error_eof("sendto");

	return error_eintr("sendto");
    }

    return count;
}

status sock_recvfrom(sock_handle sock, sock_addr_handle addr,
		     void *data, size_t data_sz)
{
    ssize_t count;
    socklen_t addrlen;
    if (!addr || !data || data_sz == 0)
	return error_invalid_arg("sock_recvfrom");

    addrlen = sizeof(addr->sa);
    count = recvfrom(sock->fd, data, data_sz, 0,
		     (struct sockaddr *) &addr->sa, &addrlen);
    if (count == -1) {
#ifdef EAGAIN
	if (errno == EAGAIN)
	    return BLOCKED;
#endif
#ifdef EWOULDBLOCK
	if (errno == EWOULDBLOCK)
	    return BLOCKED;
#endif
	if (errno == ECONNRESET)
	    return error_eof("recvfrom");

	return error_eintr("recvfrom");
    }

    if (count == 0)
	return error_eof("recvfrom");

    return count;
}

status sock_shutdown(sock_handle sock, int how)
{
    if (shutdown(sock->fd, how) == -1)
	return error_errno("shutdown");

    return OK;
}

status sock_close(sock_handle sock)
{
    if (sock->fd != -1) {
	if (close(sock->fd) == -1)
	    return error_eintr("close");

	sock->fd = -1;
    }

    return OK;
}
