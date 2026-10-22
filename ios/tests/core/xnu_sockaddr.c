// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// iOS's kernel fails bind(), connect() and sendto() with EINVAL when an IPv4
// or IPv6 address comes with any length but exactly its own (in_pcbbind() and
// in_pcbladdr() in XNU's bsd/netinet/in_pcb.c). Linux takes a longer one, so a
// wrong length passed every Linux test and only broke on the iPad. The tests
// that open sockets link this file with
// -Wl,--wrap=bind,--wrap=connect,--wrap=sendto, so on Linux they meet the same
// rule, the core included.

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netinet/in.h>

int __real_bind(int fd, const struct sockaddr *addr, socklen_t len);
int __real_connect(int fd, const struct sockaddr *addr, socklen_t len);
ssize_t __real_sendto(int fd, const void *buf, size_t size, int flags, const struct sockaddr *addr, socklen_t len);

int __wrap_bind(int fd, const struct sockaddr *addr, socklen_t len);
int __wrap_connect(int fd, const struct sockaddr *addr, socklen_t len);
ssize_t __wrap_sendto(int fd, const void *buf, size_t size, int flags, const struct sockaddr *addr, socklen_t len);

static bool length_ok(const struct sockaddr *addr, socklen_t len)
{
	// Too short to hold a family: the real call says what is wrong.
	if(!addr || len < offsetof(struct sockaddr, sa_family) + sizeof(addr->sa_family))
		return true;
	switch(addr->sa_family)
	{
		case AF_INET:
			return len == sizeof(struct sockaddr_in);
		case AF_INET6:
			return len == sizeof(struct sockaddr_in6);
		default:
			return true;
	}
}

int __wrap_bind(int fd, const struct sockaddr *addr, socklen_t len)
{
	if(!length_ok(addr, len))
	{
		errno = EINVAL;
		return -1;
	}
	return __real_bind(fd, addr, len);
}

int __wrap_connect(int fd, const struct sockaddr *addr, socklen_t len)
{
	if(!length_ok(addr, len))
	{
		errno = EINVAL;
		return -1;
	}
	return __real_connect(fd, addr, len);
}

ssize_t __wrap_sendto(int fd, const void *buf, size_t size, int flags, const struct sockaddr *addr, socklen_t len)
{
	if(!length_ok(addr, len))
	{
		errno = EINVAL;
		return -1;
	}
	return __real_sendto(fd, buf, size, flags, addr, len);
}
