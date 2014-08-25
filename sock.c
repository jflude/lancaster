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
#include <sys/types.h>
#include <sys/socket.h>

#if defined(SIOCRIPMTU) && defined(SIOCSIPMTU)
#define SIOCGIFMTU SIOCRIPMTU
#define SIOCSIFMTU SIOCSIPMTU
#endif

#if !defined(ifr_mtu) && defined(ifr_metric)
#define ifr_mtu ifr_metric
#endif

#ifdef __STRICT_ANSI__
FILE* popen(const char* command, const char* type);
int pclose(FILE* stream);
#endif

struct sock
{
	int fd;
	boolean is_open;
	void* property;
	struct sockaddr_in addr;
};

status sock_create(sock_handle* psock, int type, const char* address, int port)
{
	if (!psock || !address || port < 0) {
		error_invalid_arg("sock_create");
		return FAIL;
	}

	*psock = XMALLOC(struct sock);
	if (!*psock)
		return NO_MEMORY;

	(*psock)->is_open = FALSE;
	(*psock)->property = NULL;

	(*psock)->fd = socket(AF_INET, type, 0);
	if ((*psock)->fd == -1) {
		error_errno("socket");
		sock_destroy(psock);
		return FAIL;
	}

	(*psock)->is_open = TRUE;

	if (!inet_aton(address, &(*psock)->addr.sin_addr)) {
		error_errno("inet_aton");
		sock_destroy(psock);
		return FAIL;
	}

	(*psock)->addr.sin_family = AF_INET;
	(*psock)->addr.sin_port = htons(port);
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

long sock_get_address(sock_handle sock)
{
	return ntohl(sock->addr.sin_addr.s_addr);
}

int sock_get_port(sock_handle sock)
{
	return ntohs(sock->addr.sin_port);
}

status sock_get_text(long address, int port, char* text, size_t text_sz)
{
	char a_buf[256], p_buf[16];
	struct in_addr ia;
	if (!text || text_sz == 0) {
		error_invalid_arg("sock_get_text");
		return FAIL;
	}

	ia.s_addr = (in_addr_t) address;
	if (!inet_ntop(AF_INET, &ia, a_buf, sizeof(a_buf))) {
		error_errno("inet_ntop");
		return FAIL;
	}

	if (sprintf(p_buf, ":%d", port) < 0) {
		error_errno("sock_get_text");
		return FAIL;
	}

	if (strlen(a_buf) + strlen(p_buf) >= text_sz) {
		error_msg("sock_get_text: buffer too small", BUFFER_TOO_SMALL);
		return BUFFER_TOO_SMALL;
	}

	strcpy(text, a_buf);
	strcat(text, p_buf);
	return OK;
}

status sock_update_local_address(sock_handle sock)
{
	socklen_t len = sizeof(sock->addr);
	if (getsockname(sock->fd, (struct sockaddr*) &sock->addr, &len) == -1) {
		error_errno("getsockname");
		return FAIL;
	}

	return OK;
}

status sock_update_remote_address(sock_handle sock)
{
	socklen_t len = sizeof(sock->addr);
	if (getpeername(sock->fd, (struct sockaddr*) &sock->addr, &len) == -1) {
		error_errno("getpeername");
		return FAIL;
	}

	return OK;
}

status sock_get_device(const char* dest_address, char* pdevice, size_t device_sz)
{
	char out[256], cmd[256] = "ip route get ";
	FILE* f;
	int n;

	if (!dest_address || !pdevice || device_sz == 0) {
		error_invalid_arg("sock_get_device");
		return FAIL;
	}

	strncat(cmd, dest_address, sizeof(cmd) - strlen(cmd) - 2);
	fflush(NULL);

	errno = ENOMEM;
	f = popen(cmd, "r");
	if (!f) {
		error_errno("popen");
		return FAIL;
	}

loop:
	if ((n = fscanf(f, "%*s %*s %*s %255s", out)) == EOF) {
		if (errno == EINTR)
			goto loop;

		error_errno("fscanf");
		return FAIL;
	}

	if (n != 1) {
		error_msg("sock_get_device: invalid device format", INVALID_DEVICE_FORMAT);
		return INVALID_DEVICE_FORMAT;
	}

	if (pclose(f) == -1) {
		error_errno("pclose");
		return FAIL;
	}

	if (strlen(out) >= device_sz) {
		error_msg("sock_get_device: buffer too small", BUFFER_TOO_SMALL);
		return BUFFER_TOO_SMALL;
	}

	strcpy(pdevice, out);
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

status sock_set_mcast_ttl(sock_handle sock, int ttl)
{
	char val = ttl;
	if (setsockopt(sock->fd, IPPROTO_IP, IP_MULTICAST_TTL, &val, sizeof(val)) == -1) {
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

status sock_mcast_bind(sock_handle sock)
{
	struct ip_mreq mreq;
	if (bind(sock->fd, (struct sockaddr*) &sock->addr, sizeof(sock->addr)) == -1) {
		error_errno("bind");
		return FAIL;
	}

	mreq.imr_multiaddr.s_addr = sock->addr.sin_addr.s_addr;
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);

	if (setsockopt(sock->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == -1) {
		error_errno("setsockopt");
		return FAIL;
	}

	return OK;
}

status sock_listen(sock_handle sock, int backlog)
{
	if (bind(sock->fd, (struct sockaddr*) &sock->addr, sizeof(sock->addr)) == -1) {
		error_errno("bind");
		return FAIL;
	}

	if (listen(sock->fd, backlog) == -1) {
		error_errno("listen");
		return FAIL;
	}

	return OK;
}

status sock_accept(sock_handle sock, sock_handle* new_sock)
{
	struct sock accpt;
	socklen_t addrlen;
	if (!new_sock) {
		error_invalid_arg("sock_accept");
		return FAIL;
	}

	BZERO(&accpt);
loop:
	addrlen = sizeof(struct sockaddr_in);
	accpt.fd = accept(sock->fd, (struct sockaddr*) &accpt.addr, &addrlen);
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

	accpt.is_open = TRUE;
	**new_sock = accpt;
	return OK;
}

status sock_connect(sock_handle sock)
{
	if (connect(sock->fd, (struct sockaddr*) &sock->addr, sizeof(sock->addr)) == -1) {
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

status sock_sendto(sock_handle sock, const void* data, size_t data_sz)
{
	ssize_t count;
	if (!data || data_sz == 0) {
		error_invalid_arg("sock_sendto");
		return FAIL;
	}

loop:
	count = sendto(sock->fd, data, data_sz, 0, (struct sockaddr*) &sock->addr, sizeof(sock->addr));
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

status sock_recvfrom(sock_handle sock, void* data, size_t data_sz)
{
	ssize_t count;
	socklen_t addrlen;
	if (!data || data_sz == 0) {
		error_invalid_arg("sock_recvfrom");
		return FAIL;
	}

loop:
	addrlen = sizeof(sock->addr);
	count = recvfrom(sock->fd, data, data_sz, 0, (struct sockaddr*) &sock->addr, &addrlen);
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
	if (sock->is_open) {
	loop:
		if (close(sock->fd) == -1) {
			if (errno == EINTR)
				goto loop;

			error_errno("close");
			return FAIL;
		}

		sock->is_open = FALSE;
	}

	return OK;
}
