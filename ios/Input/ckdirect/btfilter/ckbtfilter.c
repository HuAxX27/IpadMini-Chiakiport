// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// ckbtfilter (A5.7): a Substrate tweak that loads only into iOS 9's BTServer
// (see ckbtfilter.plist). It finds BTServer's HCI link to the Bluetooth chip
// (a kernel control socket named com.apple.uart.bluetooth, or a /dev node for
// the chip's UART) and runs everything the chip sends through ck_hci_filter,
// so that a DualSense in an inquiry result or a connection request looks like
// a keyboard and BTServer agrees to pair it. It also logs the pairing and
// connection steps to /tmp/ckbtfilter.log for the device tests, or to syslog
// if BTServer cannot write there. It never logs link keys, PINs or anything
// after the first bytes of a packet.
//
// The method (hooking read and write on BTServer's HCI socket) comes from
// MFiWrapper's BTServerFilter, by Jason Fetters, 2014, GPLv3 or later.

#include "ck_hci_filter.h"

#include <substrate.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

// From XNU's sys/kern_control.h and sys/sys_domain.h, which the iOS SDK leaves out.
#define CK_SYSPROTO_CONTROL 2
#define CK_AF_SYS_CONTROL 2
#define CK_MAX_KCTL_NAME 96
struct ck_ctl_info
{
	uint32_t ctl_id;
	char ctl_name[CK_MAX_KCTL_NAME];
};
#define CK_CTLIOCGINFO _IOWR('N', 3, struct ck_ctl_info)
struct ck_sockaddr_ctl
{
	uint8_t sc_len;
	uint8_t sc_family;
	uint16_t ss_sysaddr;
	uint32_t sc_id;
	uint32_t sc_unit;
	uint32_t sc_reserved[5];
};

#define HCI_CONTROL_NAME "com.apple.uart.bluetooth"
#define LOG_PATH "/tmp/ckbtfilter.log"
#define LOG_LIMIT (512 * 1024)
#define SEEN_MAX 32

static int (*orig_open)(const char *, int, ...);
static int (*orig_socket)(int, int, int);
static int (*orig_connect)(int, const struct sockaddr *, socklen_t);
static int (*orig_close)(int);
static ssize_t (*orig_read)(int, void *, size_t);
static ssize_t (*orig_write)(int, const void *, size_t);
static ssize_t (*orig_recv)(int, void *, size_t, int);
static ssize_t (*orig_send)(int, const void *, size_t, int);

// The HCI socket and the two directions of its stream. Guarded by lock.
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int hci_fd = -1;
static CKHciFilter from_chip; // events and ACL data: rewrites inquiry results
static CKHciFilter to_chip;   // commands and ACL data: only watched
static bool lost_sync_logged[2];
static bool used_recv_send;

// Inquiry results already logged, so that a search shows each device once.
static struct
{
	uint8_t bdaddr[6];
	uint32_t cod;
	bool rewritten;
	bool split;
} seen[SEEN_MAX];
static size_t seen_count;

// --- log ---

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static int log_fd = -1;
static bool log_syslog;
static size_t log_size;

static void log_line(const char *fmt, ...)
{
	if(log_fd < 0 && !log_syslog)
		return;
	char line[256];
	struct timeval tv;
	gettimeofday(&tv, NULL);
	struct tm tm;
	localtime_r(&tv.tv_sec, &tm);
	int n = snprintf(line, sizeof(line), "%02d:%02d:%02d.%03d ", tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(tv.tv_usec / 1000));
	va_list args;
	va_start(args, fmt);
	n += vsnprintf(line + n, sizeof(line) - (size_t)n - 1, fmt, args);
	va_end(args);
	if(n > (int)sizeof(line) - 2)
		n = (int)sizeof(line) - 2;
	line[n++] = '\n';

	pthread_mutex_lock(&log_lock);
	if(log_fd < 0)
	{
		line[n - 1] = '\0';
		syslog(LOG_NOTICE, "%s", line);
	}
	else if(log_size < LOG_LIMIT)
	{
		ssize_t w = orig_write(log_fd, line, (size_t)n);
		if(w > 0)
			log_size += (size_t)w;
		if(log_size >= LOG_LIMIT)
			orig_write(log_fd, "log full, stopping\n", 19);
	}
	pthread_mutex_unlock(&log_lock);
}

// Bluetooth addresses travel least significant byte first; print them the usual way.
static const char *addr_str(const uint8_t *a, char out[18])
{
	snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", a[5], a[4], a[3], a[2], a[1], a[0]);
	return out;
}

static uint16_t le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

// --- what goes to the log ---

static void on_device_class(void *user, const CKHciDeviceClass *device)
{
	(void)user;
	bool request = device->event == CK_HCI_EVENT_CONNECTION_REQUEST;
	if(!request)
	{
		size_t i;
		for(i = 0; i < seen_count; i++)
			if(memcmp(seen[i].bdaddr, device->bdaddr, 6) == 0)
				break;
		if(i < seen_count && seen[i].cod == device->cod && seen[i].rewritten == device->rewritten
			&& seen[i].split == device->split)
			return; // already logged as it is
		if(i == seen_count)
		{
			if(seen_count == SEEN_MAX)
				return;
			seen_count++;
		}
		memcpy(seen[i].bdaddr, device->bdaddr, 6);
		seen[i].cod = device->cod;
		seen[i].rewritten = device->rewritten;
		seen[i].split = device->split;
	}

	char a[18];
	log_line("%s %s, cod 0x%06x%s", request ? "connection request from" : "inquiry result",
		addr_str(device->bdaddr, a), (unsigned)device->cod,
		device->rewritten ? ": gamepad, now a keyboard"
			: device->split ? ": gamepad, but its class came in two reads, left alone" : "");
}

static void log_l2cap(const char *dir, const CKHciPacket *p)
{
	// ACL payload: L2CAP length (2), channel ID (2); channel 1 carries signaling commands.
	const uint8_t *d = p->prefix;
	if(p->length < 8 || le16(d + 2) != 0x0001)
		return;
	switch(d[4])
	{
		case 0x01: log_line("%s l2cap command reject, reason %u", dir, le16(d + 8)); break;
		case 0x02: log_line("%s l2cap connection request, psm 0x%04x", dir, le16(d + 8)); break;
		case 0x03: log_line("%s l2cap connection response, result %u status %u", dir, le16(d + 12), le16(d + 14)); break;
		case 0x06: log_line("%s l2cap disconnection request", dir); break;
		default: break;
	}
}

static void on_packet_from_chip(void *user, const CKHciPacket *p)
{
	(void)user;
	const uint8_t *d = p->prefix;
	char a[18];
	if(p->type == CK_HCI_ACL)
	{
		log_l2cap("chip->", p);
		return;
	}
	if(p->type != CK_HCI_EVENT)
		return;
	switch(p->id)
	{
		case 0x03: log_line("connection complete %s, status 0x%02x, handle 0x%03x", addr_str(d + 3, a), d[0], le16(d + 1)); break;
		case 0x05: log_line("disconnected handle 0x%03x, status 0x%02x, reason 0x%02x", le16(d + 1), d[0], d[3]); break;
		case 0x06: log_line("authentication complete, handle 0x%03x, status 0x%02x", le16(d + 1), d[0]); break;
		case 0x07:
		{
			char name[CK_HCI_PREFIX_SIZE - 7 + 1];
			size_t n = p->length > 7 ? p->length - 7 : 0;
			if(n > sizeof(name) - 1)
				n = sizeof(name) - 1;
			memcpy(name, d + 7, n);
			name[n] = '\0';
			log_line("remote name of %s, status 0x%02x: \"%s\"", addr_str(d + 1, a), d[0], name);
			break;
		}
		case 0x08: log_line("encryption change, handle 0x%03x, status 0x%02x, on %u", le16(d + 1), d[0], d[3]); break;
		case 0x0e:
			if(p->length >= 4 && d[3] != 0)
				log_line("command 0x%04x failed, status 0x%02x", le16(d + 1), d[3]);
			break;
		case 0x0f:
			if(d[0] != 0)
				log_line("command 0x%04x refused, status 0x%02x", le16(d + 2), d[0]);
			break;
		case 0x16: log_line("PIN code request from %s", addr_str(d, a)); break;
		case 0x17: log_line("link key request for %s", addr_str(d, a)); break;
		case 0x18: log_line("new link key for %s, type %u", addr_str(d, a), d[22]); break;
		case 0x31: log_line("IO capability request for %s", addr_str(d, a)); break;
		case 0x32: log_line("IO capability of %s: io %u, oob %u, auth 0x%02x", addr_str(d, a), d[6], d[7], d[8]); break;
		case 0x33: log_line("user confirmation request for %s", addr_str(d, a)); break;
		case 0x36: log_line("simple pairing complete %s, status 0x%02x", addr_str(d + 1, a), d[0]); break;
		default: break;
	}
}

static void on_packet_to_chip(void *user, const CKHciPacket *p)
{
	(void)user;
	const uint8_t *d = p->prefix;
	char a[18];
	if(p->type == CK_HCI_ACL)
	{
		log_l2cap("->chip", p);
		return;
	}
	if(p->type != CK_HCI_COMMAND)
		return;
	switch(p->id)
	{
		case 0x0401: log_line("inquiry started"); break;
		case 0x0402: log_line("inquiry cancelled"); break;
		case 0x0405: log_line("create connection to %s", addr_str(d, a)); break;
		case 0x0406: log_line("disconnect handle 0x%03x, reason 0x%02x", le16(d), d[2]); break;
		case 0x0409: log_line("accept connection from %s, role %u", addr_str(d, a), d[6]); break;
		case 0x040a: log_line("reject connection from %s, reason 0x%02x", addr_str(d, a), d[6]); break;
		case 0x040b: log_line("link key reply for %s", addr_str(d, a)); break;
		case 0x040c: log_line("no link key for %s", addr_str(d, a)); break;
		case 0x040d: log_line("PIN code reply for %s", addr_str(d, a)); break;
		case 0x040e: log_line("PIN code refused for %s", addr_str(d, a)); break;
		case 0x0411: log_line("authentication requested, handle 0x%03x", le16(d)); break;
		case 0x0419: log_line("remote name request for %s", addr_str(d, a)); break;
		case 0x042b: log_line("IO capability reply for %s: io %u, oob %u, auth 0x%02x", addr_str(d, a), d[6], d[7], d[8]); break;
		case 0x042c: log_line("user confirmation accepted for %s", addr_str(d, a)); break;
		case 0x042d: log_line("user confirmation refused for %s", addr_str(d, a)); break;
		case 0x0434: log_line("IO capability refused for %s, reason 0x%02x", addr_str(d, a), d[6]); break;
		default: break;
	}
}

// --- the HCI socket ---

static bool hci_control_id(uint32_t *id)
{
	static bool known;
	static uint32_t cached;
	if(known)
	{
		*id = cached;
		return true;
	}
	int fd = orig_socket(AF_SYSTEM, SOCK_DGRAM, CK_SYSPROTO_CONTROL);
	if(fd < 0)
		return false;
	struct ck_ctl_info info;
	memset(&info, 0, sizeof(info));
	strncpy(info.ctl_name, HCI_CONTROL_NAME, sizeof(info.ctl_name) - 1);
	bool ok = ioctl(fd, CK_CTLIOCGINFO, &info) == 0;
	orig_close(fd);
	if(!ok)
		return false;
	cached = info.ctl_id;
	known = true;
	*id = cached;
	return true;
}

static void start_hci(int fd)
{
	CKHciFilterCallbacks from = { NULL, on_packet_from_chip, on_device_class };
	CKHciFilterCallbacks to = { NULL, on_packet_to_chip, NULL };
	pthread_mutex_lock(&lock);
	hci_fd = fd;
	ck_hci_filter_init(&from_chip, true, &from);
	ck_hci_filter_init(&to_chip, false, &to);
	lost_sync_logged[0] = lost_sync_logged[1] = false;
	pthread_mutex_unlock(&lock);
}

static void feed(int fd, CKHciFilter *filter, int direction, const void *buf, ssize_t len)
{
	if(len <= 0 || fd != hci_fd)
		return;
	pthread_mutex_lock(&lock);
	if(fd == hci_fd)
	{
		// The to_chip filter has rewrite off and never writes into the buffer.
		ck_hci_filter_feed(filter, (uint8_t *)buf, (size_t)len);
		if(filter->lost_sync && !lost_sync_logged[direction])
		{
			lost_sync_logged[direction] = true;
			log_line("lost track of the %s stream after %u packets; no more rewrites", direction ? "->chip" : "chip->",
				(unsigned)filter->packets);
		}
	}
	pthread_mutex_unlock(&lock);
}

// --- hooks ---

// Older iOS versions reach the chip through a /dev node for its UART; the
// stream on it is the same H4, so follow that too if BTServer opens one.
static bool is_bluetooth_node(const char *path)
{
	return path && strncmp(path, "/dev/", 5) == 0 && (strstr(path, "bluetooth") || strstr(path, "uart"));
}

static int new_open(const char *path, int flags, ...)
{
	int mode = 0;
	if(flags & O_CREAT)
	{
		va_list args;
		va_start(args, flags);
		mode = va_arg(args, int);
		va_end(args);
	}
	int fd = orig_open(path, flags, mode);
	int saved = errno;
	if(is_bluetooth_node(path))
	{
		log_line("open %s: %s, fd %d", path, fd >= 0 ? "ok" : strerror(saved), fd);
		if(fd >= 0)
			start_hci(fd);
	}
	errno = saved;
	return fd;
}

static int new_socket(int domain, int type, int protocol)
{
	int fd = orig_socket(domain, type, protocol);
	int saved = errno;
	if(domain == AF_SYSTEM && protocol == CK_SYSPROTO_CONTROL)
		log_line("kernel control socket fd %d", fd);
	errno = saved;
	return fd;
}

static int new_connect(int fd, const struct sockaddr *addr, socklen_t len)
{
	int r = orig_connect(fd, addr, len);
	int saved = errno;
	if(addr && addr->sa_family == AF_SYSTEM && len >= sizeof(struct ck_sockaddr_ctl))
	{
		const struct ck_sockaddr_ctl *ctl = (const struct ck_sockaddr_ctl *)addr;
		uint32_t id;
		bool is_hci = ctl->ss_sysaddr == CK_AF_SYS_CONTROL && hci_control_id(&id) && id == ctl->sc_id;
		log_line("connect fd %d to kernel control %u unit %u: %s%s", fd, (unsigned)ctl->sc_id, (unsigned)ctl->sc_unit,
			r == 0 ? "ok" : strerror(saved), is_hci ? " (" HCI_CONTROL_NAME ")" : "");
		if(r == 0 && is_hci)
			start_hci(fd);
	}
	errno = saved;
	return r;
}

static int new_close(int fd)
{
	if(fd >= 0 && fd == hci_fd)
	{
		pthread_mutex_lock(&lock);
		if(fd == hci_fd)
		{
			log_line("HCI socket closed after %u packets from the chip and %u to it, %u rewrites",
				(unsigned)from_chip.packets, (unsigned)to_chip.packets, (unsigned)from_chip.rewrites);
			hci_fd = -1;
		}
		pthread_mutex_unlock(&lock);
	}
	return orig_close(fd);
}

static ssize_t new_read(int fd, void *buf, size_t n)
{
	ssize_t r = orig_read(fd, buf, n);
	int saved = errno;
	feed(fd, &from_chip, 0, buf, r);
	errno = saved;
	return r;
}

static ssize_t new_write(int fd, const void *buf, size_t n)
{
	ssize_t r = orig_write(fd, buf, n);
	int saved = errno;
	feed(fd, &to_chip, 1, buf, r);
	errno = saved;
	return r;
}

static void note_recv_send(int fd)
{
	if(fd == hci_fd && !used_recv_send)
	{
		used_recv_send = true;
		log_line("BTServer uses recv/send on the HCI socket");
	}
}

static ssize_t new_recv(int fd, void *buf, size_t n, int flags)
{
	ssize_t r = orig_recv(fd, buf, n, flags);
	int saved = errno;
	note_recv_send(fd);
	if(!(flags & MSG_PEEK))
		feed(fd, &from_chip, 0, buf, r);
	errno = saved;
	return r;
}

static ssize_t new_send(int fd, const void *buf, size_t n, int flags)
{
	ssize_t r = orig_send(fd, buf, n, flags);
	int saved = errno;
	note_recv_send(fd);
	feed(fd, &to_chip, 1, buf, r);
	errno = saved;
	return r;
}

__attribute__((constructor)) static void ckbtfilter_init(void)
{
	// Hook first so that orig_write exists before anything is logged.
	MSHookFunction((void *)open, (void *)new_open, (void **)&orig_open);
	MSHookFunction((void *)socket, (void *)new_socket, (void **)&orig_socket);
	MSHookFunction((void *)connect, (void *)new_connect, (void **)&orig_connect);
	MSHookFunction((void *)close, (void *)new_close, (void **)&orig_close);
	MSHookFunction((void *)read, (void *)new_read, (void **)&orig_read);
	MSHookFunction((void *)write, (void *)new_write, (void **)&orig_write);
	MSHookFunction((void *)recv, (void *)new_recv, (void **)&orig_recv);
	MSHookFunction((void *)send, (void *)new_send, (void **)&orig_send);

	log_fd = orig_open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if(log_fd < 0)
	{
		int saved = errno;
		openlog("ckbtfilter", LOG_PID, LOG_USER);
		log_syslog = true;
		log_line("cannot write %s (%s); logging here instead", LOG_PATH, strerror(saved));
	}
	log_line("ckbtfilter loaded into pid %d, uid %d", (int)getpid(), (int)getuid());
}
