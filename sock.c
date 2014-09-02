#include "sock.h"
#include "error.h"
#include "xalloc.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#if defined(SIOCRIPMTU) && defined(SIOCSIPMTU)
#define SIOCGIFMTU SIOCRIPMTU
#define SIOCSIFMTU SIOCSIPMTU
#endif

#if !defined(ifr_mtu) && defined(ifr_metric)
#define ifr_mtu ifr_metric
#endif

struct sock
{
	int fd;
	void* property;
};

struct sock_addr
{
	struct sockaddr_in sa;
};

status sock_addr_create(sock_addr_handle* paddr, const char* address, unsigned short port)
{
	if (!paddr) {
		error_invalid_arg("sock_addr_create");
		return FAIL;
	}

	*paddr = XMALLOC(struct sock_addr);
	if (!*paddr)
		return NO_MEMORY;

	BZERO(*paddr);

	(*paddr)->sa.sin_family = AF_INET;
	(*paddr)->sa.sin_port = htons(port);

	if (!address)
		(*paddr)->sa.sin_addr.s_addr = htonl(INADDR_ANY);
	else if (!inet_aton(address, &(*paddr)->sa.sin_addr)) {
		error_errno("inet_aton");
		sock_addr_destroy(paddr);
		return FAIL;
	}

	return OK;
}

void sock_addr_destroy(sock_addr_handle* paddr)
{
	if (!paddr || !*paddr)
		return;

	xfree(*paddr);
	*paddr = NULL;
}

unsigned long sock_addr_get_ip(sock_addr_handle addr)
{
	return ntohl(addr->sa.sin_addr.s_addr);
}

unsigned short sock_addr_get_port(sock_addr_handle addr)
{
	return ntohs(addr->sa.sin_port);
}

status sock_addr_get_text(sock_addr_handle addr, char* text, size_t text_sz)
{
	char a_buf[256], p_buf[16];
	if (!text || text_sz == 0) {
		error_invalid_arg("sock_addr_get_text");
		return FAIL;
	}

	if (!inet_ntop(addr->sa.sin_family, &addr->sa, a_buf, sizeof(a_buf))) {
		error_errno("inet_ntop");
		return FAIL;
	}

	if (sprintf(p_buf, ":%hu", ntohs(addr->sa.sin_port)) < 0) {
		error_errno("sock_addr_get_text");
		return FAIL;
	}

	if (strlen(a_buf) + strlen(p_buf) >= text_sz) {
		error_msg("sock_addr_get_text: buffer too small", BUFFER_TOO_SMALL);
		return BUFFER_TOO_SMALL;
	}

	strcpy(text, a_buf);
	strcat(text, p_buf);
	return OK;
}

void sock_addr_set_none(sock_addr_handle addr)
{
	addr->sa.sin_addr.s_addr = htonl(INADDR_NONE);
	addr->sa.sin_port = 0;
}

boolean sock_addr_is_equal(sock_addr_handle addr1, sock_addr_handle addr2)
{
	return addr1 && addr2 &&
		addr1->sa.sin_addr.s_addr == addr2->sa.sin_addr.s_addr &&
		addr1->sa.sin_port == addr2->sa.sin_port;
}

status sock_create(sock_handle* psock, int type)
{
	if (!psock) {
		error_invalid_arg("sock_create");
		return FAIL;
	}

	*psock = XMALLOC(struct sock);
	if (!*psock)
		return NO_MEMORY;

	BZERO(*psock);

	(*psock)->fd = socket(AF_INET, type, 0);
	if ((*psock)->fd == -1) {
		error_errno("socket");
		sock_destroy(psock);
		return FAIL;
	}

	return OK;
}

void sock_destroy(sock_handle* psock)
{
	if (!psock || !*psock)
		return;

	error_save_last();
	sock_close(*psock);
	error_restore_last();

	xfree(*psock);
	*psock = NULL;
}

void* sock_get_property(sock_handle sock)
{
	return sock->property;
}

void sock_set_property(sock_handle sock, void* prop)
{
	sock->property = prop;
}

int sock_get_descriptor(sock_handle sock)
{
	return sock->fd;
}

status sock_get_local_address(sock_handle sock, sock_addr_handle addr)
{
	socklen_t len;
	if (!addr) {
		error_invalid_arg("sock_get_local_address");
		return FAIL;
	}

	len = sizeof(addr->sa);
	if (getsockname(sock->fd, (struct sockaddr*) &addr->sa, &len) == -1) {
		error_errno("getsockname");
		return FAIL;
	}

	return OK;
}

status sock_get_remote_address(sock_handle sock, sock_addr_handle addr)
{
	socklen_t len;
	if (!addr) {
		error_invalid_arg("sock_get_remote_address");
		return FAIL;
	}

	len = sizeof(addr->sa);
	if (getpeername(sock->fd, (struct sockaddr*) &addr->sa, &len) == -1) {
		error_errno("getpeername");
		return FAIL;
	}

	return OK;
}

status sock_get_interface_address(sock_handle sock, const char* device, sock_addr_handle addr)
{
	struct ifreq ifr;
	if (!device) {
		error_invalid_arg("sock_get_interface_address");
		return FAIL;
	}

	BZERO(&ifr);
	strncpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));

	if (ioctl(sock->fd, SIOCGIFADDR, &ifr) == -1) {
		error_errno("ioctl");
		return FAIL;
	}

	addr->sa.sin_addr = ((struct sockaddr_in*) &ifr.ifr_addr)->sin_addr;
	return OK;
}

status sock_get_mtu(sock_handle sock, const char* device, size_t* pmtu)
{
	struct ifreq ifr;
	if (!device || !pmtu) {
		error_invalid_arg("sock_get_mtu");
		return FAIL;
	}

	BZERO(&ifr);
	strncpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));

	if (ioctl(sock->fd, SIOCGIFMTU, &ifr) == -1) {
		error_errno("ioctl");
		return FAIL;
	}

	*pmtu = ifr.ifr_mtu;
	return OK;
}

status sock_set_nonblock(sock_handle sock)
{
	int flags;
#ifdef O_NONBLOCK
	flags = fcntl(sock->fd, F_GETFL, 0);
	if (flags == -1) {
		error_errno("fcntl");
		return FAIL;
	}

	if (fcntl(sock->fd, F_SETFL, flags | O_NONBLOCK) == -1) {
		error_errno("fcntl");
		return FAIL;
	}
#else
	flags = 1;
	if (ioctl(sock->fd, FIOBIO, &flags) == -1) {
		error_errno("ioctl");
		return FAIL;
	}
#endif
	return OK;
}

status sock_set_reuseaddr(sock_handle sock, int reuse)
{
	if (setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
		error_errno("setsockopt");
		return FAIL;
	}

	return OK;
}

status sock_set_mcast_ttl(sock_handle sock, short ttl)
{
	unsigned char val = ttl;
	if (setsockopt(sock->fd, IPPROTO_IP, IP_MULTICAST_TTL, &val, sizeof(val)) == -1) {
		error_errno("setsockopt");
		return FAIL;
	}

	return OK;
}

status sock_set_mcast_loopback(sock_handle sock, boolean allow_loop)
{
	if (setsockopt(sock->fd, IPPROTO_IP, IP_MULTICAST_LOOP, &allow_loop, sizeof(allow_loop)) == -1) {
		error_errno("setsockopt");
		return FAIL;
	}

	return OK;
}

status sock_set_mcast_interface(sock_handle sock, sock_addr_handle addr)
{
	if (!addr) {
		error_invalid_arg("sock_set_mcast_interface");
		return FAIL;
	}

	if (setsockopt(sock->fd, IPPROTO_IP, IP_MULTICAST_IF, &addr->sa.sin_addr, sizeof(addr->sa.sin_addr)) == -1) {
		error_errno("setsockopt");
		return FAIL;
	}

	return OK;
}

status sock_set_rcvbuf(sock_handle sock, size_t buf_sz)
{
	int val = buf_sz;
	if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val)) == -1) {
		error_errno("setsockopt");
		return FAIL;
	}

	return OK;
}

status sock_bind(sock_handle sock, sock_addr_handle addr)
{
	if (!addr) {
		error_invalid_arg("sock_bind");
		return FAIL;
	}

	if (bind(sock->fd, (struct sockaddr*) &addr->sa, sizeof(addr->sa)) == -1) {
		error_errno("bind");
		return FAIL;
	}

	return OK;
}

status sock_listen(sock_handle sock, int backlog)
{
	if (listen(sock->fd, backlog) == -1) {
		error_errno("listen");
		return FAIL;
	}

	return OK;
}

status sock_accept(sock_handle sock, sock_handle* new_sock)
{
	struct sock accpt;
	if (!new_sock) {
		error_invalid_arg("sock_accept");
		return FAIL;
	}

	BZERO(&accpt);
loop:
	accpt.fd = accept(sock->fd, NULL, NULL);
	if (accpt.fd == -1) {
		if (errno == EINTR)
			goto loop;
#ifdef EAGAIN
		if (errno == EAGAIN)
			return BLOCKED;
#endif
#ifdef EWOULDBLOCK
		if (errno == EWOULDBLOCK)
			return BLOCKED;
#endif
		error_errno("accept");
		if (errno == ETIMEDOUT)
			return TIMED_OUT;
		else
			return FAIL;
	}

	*new_sock = XMALLOC(struct sock);
	if (!*new_sock) {
	close_loop:
		if (close(accpt.fd) == -1)
			if (errno == EINTR)
				goto close_loop;

		return NO_MEMORY;
	}

	**new_sock = accpt;
	return OK;
}

status sock_connect(sock_handle sock, sock_addr_handle addr)
{
	if (!addr) {
		error_invalid_arg("sock_connect");
		return FAIL;
	}

	if (connect(sock->fd, (const struct sockaddr*) &addr->sa, sizeof(addr->sa)) == -1) {
		error_errno("connect");
		if (errno == ECONNREFUSED)
			return EOF;
		else if (errno == ETIMEDOUT)
			return TIMED_OUT;
		else
			return FAIL;
	}

	return OK;
}

status sock_mcast_add(sock_handle sock, sock_addr_handle multi_addr, sock_addr_handle iface_addr)
{
	struct ip_mreq mreq;
	if (!multi_addr || !iface_addr) {
		error_invalid_arg("sock_mcast_add");
		return FAIL;
	}

	mreq.imr_multiaddr = multi_addr->sa.sin_addr;
	mreq.imr_interface = iface_addr->sa.sin_addr;

	if (setsockopt(sock->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == -1) {
		error_errno("setsockopt");
		return FAIL;
	}

	return OK;
}

status sock_mcast_drop(sock_handle sock, sock_addr_handle multi_addr, sock_addr_handle iface_addr)
{
	struct ip_mreq mreq;
	if (!multi_addr || !iface_addr) {
		error_invalid_arg("sock_mcast_drop");
		return FAIL;
	}

	mreq.imr_multiaddr.s_addr = multi_addr->sa.sin_addr.s_addr;
	mreq.imr_interface.s_addr = iface_addr->sa.sin_addr.s_addr;

	if (setsockopt(sock->fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)) == -1) {
		error_errno("setsockopt");
		return FAIL;
	}

	return OK;
}

status sock_write(sock_handle sock, const void* data, size_t data_sz)
{
	ssize_t count;
	if (!data || data_sz == 0) {
		error_invalid_arg("sock_write");
		return FAIL;
	}

loop:
	count = write(sock->fd, data, data_sz);
	if (count == -1) {
		if (errno == EINTR)
			goto loop;
#ifdef EAGAIN
		if (errno == EAGAIN)
			return BLOCKED;
#endif
#ifdef EWOULDBLOCK
		if (errno == EWOULDBLOCK)
			return BLOCKED;
#endif
		if (errno == EPIPE || errno == ECONNRESET) {
			error_eof("write");
			return EOF;
		}

		error_errno("write");
		if (errno == ETIMEDOUT)
			return TIMED_OUT;
		else
			return FAIL;
	}

	return count;
}

status sock_read(sock_handle sock, void* data, size_t data_sz)
{
	ssize_t count;
	if (!data || data_sz == 0) {
		error_invalid_arg("sock_read");
		return FAIL;
	}

loop:
	count = read(sock->fd, data, data_sz);
	if (count == -1) {
		if (errno == EINTR)
			goto loop;
#ifdef EAGAIN
		if (errno == EAGAIN)
			return BLOCKED;
#endif
#ifdef EWOULDBLOCK
		if (errno == EWOULDBLOCK)
			return BLOCKED;
#endif
		if (errno == ECONNRESET) {
			error_eof("read");
			return EOF;
		}

		error_errno("read");
		if (errno == ETIMEDOUT)
			return TIMED_OUT;
		else
			return FAIL;
	}

	if (count == 0) {
		error_eof("read");
		return EOF;
	}

	return count;
}

status sock_sendto(sock_handle sock, sock_addr_handle addr, const void* data, size_t data_sz)
{
	ssize_t count;
	if (!addr || !data || data_sz == 0) {
		error_invalid_arg("sock_sendto");
		return FAIL;
	}

loop:
	count = sendto(sock->fd, data, data_sz, 0, (const struct sockaddr*) &addr->sa, sizeof(addr->sa));
	if (count == -1) {
		if (errno == EINTR)
			goto loop;
#ifdef EAGAIN
		if (errno == EAGAIN)
			return BLOCKED;
#endif
#ifdef EWOULDBLOCK
		if (errno == EWOULDBLOCK)
			return BLOCKED;
#endif
		if (errno == EPIPE || errno == ECONNRESET) {
			error_eof("sendto");
			return EOF;
		}

		error_errno("sendto");
		if (errno == ETIMEDOUT)
			return TIMED_OUT;
		else
			return FAIL;
	}

	return count;
}

status sock_recvfrom(sock_handle sock, sock_addr_handle addr, void* data, size_t data_sz)
{
	ssize_t count;
	socklen_t addrlen;
	if (!addr || !data || data_sz == 0) {
		error_invalid_arg("sock_recvfrom");
		return FAIL;
	}

loop:
	addrlen = sizeof(addr->sa);
	count = recvfrom(sock->fd, data, data_sz, 0, (struct sockaddr*) &addr->sa, &addrlen);
	if (count == -1) {
		if (errno == EINTR)
			goto loop;
#ifdef EAGAIN
		if (errno == EAGAIN)
			return BLOCKED;
#endif
#ifdef EWOULDBLOCK
		if (errno == EWOULDBLOCK)
			return BLOCKED;
#endif
		if (errno == ECONNRESET) {
			error_eof("recvfrom");
			return EOF;
		}

		error_errno("recvfrom");
		if (errno == ETIMEDOUT)
			return TIMED_OUT;
		else
			return FAIL;
	}

	if (count == 0) {
		error_eof("recvfrom");
		return EOF;
	}

	return count;
}

status sock_shutdown(sock_handle sock, int how)
{
	if (shutdown(sock->fd, how) == -1) {
		error_errno("shutdown");
		return FAIL;
	}

	return OK;
}

status sock_close(sock_handle sock)
{
	if (sock->fd != -1) {
	loop:
		if (close(sock->fd) == -1) {
			if (errno == EINTR)
				goto loop;

			error_errno("close");
			return FAIL;
		}

		sock->fd = -1;
	}

	return OK;
}
