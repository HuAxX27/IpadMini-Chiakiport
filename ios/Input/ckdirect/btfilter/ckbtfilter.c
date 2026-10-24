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
// It follows the pad's HID channels and copies every input report into a
// mapped file (ck_ds5_shared.h), where the app reads it (A5.8). When the pad
// sends the simple report, it slips two output reports to the pad into the
// stream to BTServer's chip, between two of BTServer's own packets: they light
// the player LED and switch the pad to the full report, with the touchpad.
// The chip later counts them among the packets it is done with; the filter
// takes them out of that count, so BTServer's flow control stays right.
//
// The method (hooking read and write on BTServer's HCI socket) comes from
// MFiWrapper's BTServerFilter, by Jason Fetters, 2014, GPLv3 or later.

#include "ck_ds5.h"
#include "ck_ds5_shared.h"
#include "ck_hci_filter.h"

#include <substrate.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
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
// The Linux test (ios/tests/ckbtfilter_test.c) writes both files elsewhere.
#ifndef LOG_PATH
#define LOG_PATH "/tmp/ckbtfilter.log"
#endif
#ifndef SHARED_PATH
#define SHARED_PATH CK_DS5_SHARED_PATH
#endif
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
// write_lock is held around every write to the HCI socket, so that the
// filter's own packets never land in the middle of one of BTServer's. Take
// write_lock before lock, never the other way round.
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t write_lock = PTHREAD_MUTEX_INITIALIZER;
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

// Devices whose class the filter rewrote, and the HID channels of their
// connection. Only their HID traffic is logged, so nothing a real keyboard
// sends ever reaches the log.
#define PADS_MAX 4
#define PSM_HID_CONTROL 0x0011
#define PSM_HID_INTERRUPT 0x0013
#define HID_LOG_MAX 16      // HID messages other than input reports, per connection
#define HID_FIRST_INPUTS 3  // input reports logged one by one, per connection
#define HID_SUMMARY_SECONDS 10
// Asking a pad for the full report: how many times, and how many more simple
// reports to wait for before asking again.
#define SWITCH_TRIES 3
#define SWITCH_RETRY_REPORTS 100
// The lightbar once the pad is on the iPad: the PS5's blue for player 1.
#define LIGHTBAR_RED 0x00
#define LIGHTBAR_GREEN 0x00
#define LIGHTBAR_BLUE 0x80

typedef struct
{
	uint16_t local;  // our channel ID: the pad's data arrives on it
	uint16_t remote; // the pad's channel ID: our data goes to it
} Channel;

static struct
{
	uint8_t bdaddr[6];
	bool connected;
	uint16_t handle;
	Channel control;
	Channel interrupt;
	unsigned inputs;        // input reports in this connection
	unsigned window_inputs; // since window_start
	time_t window_start;
	unsigned logged;        // other HID messages logged in this connection
	uint8_t last_id;
	uint16_t last_len;
	uint8_t acl_flags;      // the flags BTServer puts on its packets to the pad
	bool want_switch;       // send the output reports at the next chance
	unsigned switch_tries;
	unsigned simple_since_try;
	uint8_t output_seq;
	unsigned injected;      // output reports the chip has not yet counted as done
	bool full;              // the pad sends the full report
} pads[PADS_MAX];
static size_t pad_count;
// Some pad has want_switch set. Read without the lock after every read.
static atomic_bool switch_wanted;

// Where the pad's input reports go for other processes (ck_ds5_shared.h).
static CKDS5Shared *shared;
static bool shared_tried;

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

static void remember_pad(const uint8_t *bdaddr);

static void on_device_class(void *user, const CKHciDeviceClass *device)
{
	(void)user;
	bool request = device->event == CK_HCI_EVENT_CONNECTION_REQUEST;
	if(device->rewritten)
		remember_pad(device->bdaddr);
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

// --- the pads ---

static int pad_by_addr(const uint8_t *bdaddr)
{
	for(size_t i = 0; i < pad_count; i++)
		if(memcmp(pads[i].bdaddr, bdaddr, 6) == 0)
			return (int)i;
	return -1;
}

static int pad_by_handle(uint16_t handle)
{
	for(size_t i = 0; i < pad_count; i++)
		if(pads[i].connected && pads[i].handle == handle)
			return (int)i;
	return -1;
}

static void remember_pad(const uint8_t *bdaddr)
{
	if(pad_by_addr(bdaddr) >= 0 || pad_count == PADS_MAX)
		return;
	memset(&pads[pad_count], 0, sizeof(pads[pad_count]));
	memcpy(pads[pad_count].bdaddr, bdaddr, 6);
	pad_count++;
}

// Maps SHARED_PATH once BTServer has the HCI socket. Attaching says "no pad",
// so a BTServer that crashed with a button held does not leave it held.
static void open_shared(void)
{
	if(shared_tried)
		return;
	shared_tried = true;
	int fd = orig_open(SHARED_PATH, O_RDWR | O_CREAT, 0644);
	if(fd < 0)
	{
		log_line("cannot open %s: %s", SHARED_PATH, strerror(errno));
		return;
	}
	void *map = MAP_FAILED;
	if(ftruncate(fd, (off_t)sizeof(CKDS5Shared)) == 0)
		map = mmap(NULL, sizeof(CKDS5Shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	int saved = errno;
	orig_close(fd);
	if(map == MAP_FAILED)
	{
		log_line("cannot map %s: %s", SHARED_PATH, strerror(saved));
		return;
	}
	shared = map;
	ck_ds5_shared_attach(shared);
	log_line("sharing the pad's input reports in %s", SHARED_PATH);
}

static void pad_connected(const uint8_t *bdaddr, uint16_t handle)
{
	int i = pad_by_addr(bdaddr);
	if(i < 0)
		return;
	uint8_t addr[6];
	memcpy(addr, bdaddr, 6);
	memset(&pads[i], 0, sizeof(pads[i]));
	memcpy(pads[i].bdaddr, addr, 6);
	pads[i].connected = true;
	pads[i].handle = handle;
	pads[i].window_start = time(NULL);
	pads[i].acl_flags = CK_HCI_ACL_START;
}

static void pad_disconnected(uint16_t handle)
{
	int i = pad_by_handle(handle);
	if(i < 0)
		return;
	char a[18];
	log_line("pad %s gone after %u input reports", addr_str(pads[i].bdaddr, a), pads[i].inputs);
	pads[i].connected = false;
	// The chip forgets what it had for this connection, and so does BTServer.
	pads[i].injected = 0;
	pads[i].want_switch = false;
	if(shared)
		ck_ds5_shared_write(shared, NULL, 0);
}

static const char *hidp_type(uint8_t header)
{
	switch(header >> 4)
	{
		case 0x0: return "HANDSHAKE";
		case 0x1: return "HID_CONTROL";
		case 0x4: return "GET_REPORT";
		case 0x5: return "SET_REPORT";
		case 0x6: return "GET_PROTOCOL";
		case 0x7: return "SET_PROTOCOL";
		case 0xa: return "DATA";
		default: return "other";
	}
}

// A simple report came in: ask for the full one the first time, and again
// after SWITCH_RETRY_REPORTS more, up to SWITCH_TRIES times.
static void want_full_report(int i)
{
	if(pads[i].want_switch || pads[i].switch_tries >= SWITCH_TRIES || !pads[i].interrupt.remote)
		return;
	if(pads[i].switch_tries > 0 && ++pads[i].simple_since_try < SWITCH_RETRY_REPORTS)
		return;
	pads[i].want_switch = true;
	atomic_store(&switch_wanted, true);
}

// One HIDP message on a pad's control or interrupt channel: input reports are
// counted, everything else is logged up to HID_LOG_MAX per connection.
static void pad_hid(int i, bool from_pad, const char *channel, const uint8_t *msg, uint16_t len)
{
	if(len == 0)
		return;
	char a[18];
	if(from_pad && msg[0] == 0xa1 && len >= 2)
	{
		pads[i].inputs++;
		pads[i].window_inputs++;
		pads[i].last_id = msg[1];
		pads[i].last_len = (uint16_t)(len - 1);
		if(shared)
			ck_ds5_shared_write(shared, msg + 1, len - 1u);
		if(pads[i].inputs <= HID_FIRST_INPUTS)
			log_line("hid input report 0x%02x from %s on %s, %u bytes", msg[1], addr_str(pads[i].bdaddr, a), channel,
				len - 1);
		if(msg[1] == CK_DS5_REPORT_ID_INPUT)
			want_full_report(i);
		else if(msg[1] == CK_DS5_REPORT_ID_BT_INPUT && !pads[i].full)
		{
			pads[i].full = true;
			log_line("%s sends the full report now, after %u requests", addr_str(pads[i].bdaddr, a),
				pads[i].switch_tries);
		}
		time_t now = time(NULL);
		if(now - pads[i].window_start >= HID_SUMMARY_SECONDS)
		{
			log_line("hid input from %s: %u reports in %ld s, last 0x%02x with %u bytes", addr_str(pads[i].bdaddr, a),
				pads[i].window_inputs, (long)(now - pads[i].window_start), pads[i].last_id, pads[i].last_len);
			pads[i].window_inputs = 0;
			pads[i].window_start = now;
		}
		return;
	}
	if(pads[i].logged >= HID_LOG_MAX)
		return;
	pads[i].logged++;
	log_line("hid %s %s on %s: %s 0x%02x, report 0x%02x, %u bytes", from_pad ? "from" : "to",
		addr_str(pads[i].bdaddr, a), channel, hidp_type(msg[0]), msg[0], len >= 2 ? msg[1] : 0, len);
}

static Channel *hid_channel(int i, uint16_t psm)
{
	if(psm == PSM_HID_CONTROL)
		return &pads[i].control;
	if(psm == PSM_HID_INTERRUPT)
		return &pads[i].interrupt;
	return NULL;
}

// L2CAP signaling (channel 1): logs connections and keeps the CIDs of the pads' HID channels.
static void l2cap_signal(int pad, bool from_chip, const uint8_t *d, uint16_t length)
{
	const char *dir = from_chip ? "chip->" : "->chip";
	if(length < 8)
		return;
	uint16_t a = le16(d + 8), b = length >= 12 ? le16(d + 10) : 0;
	switch(d[4])
	{
		case 0x01: log_line("%s l2cap command reject, reason %u", dir, a); break;
		case 0x02: // connection request: psm, source CID
		{
			log_line("%s l2cap connection request, psm 0x%04x, cid 0x%04x", dir, a, b);
			Channel *ch = pad >= 0 ? hid_channel(pad, a) : NULL;
			if(ch)
			{
				ch->local = from_chip ? 0 : b;
				ch->remote = from_chip ? b : 0;
			}
			break;
		}
		case 0x03: // connection response: destination CID, source CID, result, status
		{
			uint16_t result = length >= 16 ? le16(d + 12) : 0xffff;
			log_line("%s l2cap connection response, cids 0x%04x 0x%04x, result %u status %u", dir, a, b, result,
				length >= 16 ? le16(d + 14) : 0);
			if(pad < 0 || result != 0)
				break;
			// The responder's CID is the destination one; the source CID names the requester's.
			Channel *chs[2] = { &pads[pad].control, &pads[pad].interrupt };
			for(int k = 0; k < 2; k++)
			{
				if(from_chip && chs[k]->local == b && chs[k]->remote == 0)
					chs[k]->remote = a;
				else if(!from_chip && chs[k]->remote == b && chs[k]->local == 0)
					chs[k]->local = a;
			}
			break;
		}
		case 0x06: log_line("%s l2cap disconnection request, cids 0x%04x 0x%04x", dir, a, b); break;
		default: break;
	}
}

static void on_acl(bool from_chip, const CKHciPacket *p)
{
	// ACL payload: L2CAP length (2), channel ID (2), then the channel's data.
	if((p->flags & 0x3) == 0x1 || p->length < 5)
		return; // a continuing fragment carries no L2CAP header
	const uint8_t *d = p->prefix;
	uint16_t cid = le16(d + 2);
	int pad = pad_by_handle(p->id);
	if(pad >= 0 && !from_chip)
		pads[pad].acl_flags = p->flags;
	if(cid == 0x0001)
	{
		l2cap_signal(pad, from_chip, d, p->length);
		return;
	}
	if(pad < 0)
		return;
	uint16_t len = le16(d);
	if(len > p->length - 4)
		len = (uint16_t)(p->length - 4);
	if(len > CK_HCI_PREFIX_SIZE - 4)
		len = CK_HCI_PREFIX_SIZE - 4; // only the prefix was kept
	const Channel *control = &pads[pad].control, *interrupt = &pads[pad].interrupt;
	if(from_chip)
	{
		if(cid == interrupt->local && cid)
			pad_hid(pad, true, "interrupt", d + 4, len);
		else if(cid == control->local && cid)
			pad_hid(pad, true, "control", d + 4, len);
	}
	else
	{
		if(cid == interrupt->remote && cid)
			pad_hid(pad, false, "interrupt", d + 4, len);
		else if(cid == control->remote && cid)
			pad_hid(pad, false, "control", d + 4, len);
	}
}

static void on_packet_from_chip(void *user, const CKHciPacket *p)
{
	(void)user;
	const uint8_t *d = p->prefix;
	char a[18];
	if(p->type == CK_HCI_ACL)
	{
		on_acl(true, p);
		return;
	}
	if(p->type != CK_HCI_EVENT)
		return;
	switch(p->id)
	{
		case 0x03:
			log_line("connection complete %s, status 0x%02x, handle 0x%03x", addr_str(d + 3, a), d[0], le16(d + 1));
			if(d[0] == 0)
				pad_connected(d + 3, le16(d + 1) & 0x0fff);
			break;
		case 0x05:
			log_line("disconnected handle 0x%03x, status 0x%02x, reason 0x%02x", le16(d + 1), d[0], d[3]);
			if(d[0] == 0)
				pad_disconnected(le16(d + 1) & 0x0fff);
			break;
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
			else if(p->length >= 9 && le16(d + 1) == 0x1005)
				log_line("chip takes %u ACL packets of %u bytes at a time", le16(d + 7), le16(d + 4));
			break;
		case 0x1a: log_line("chip ACL buffers overflowed, link type %u", d[0]); break;
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
		on_acl(false, p);
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

// The chip is done with count packets of handle: leave out the output
// reports the filter sent, which BTServer never counted as sent.
static uint16_t on_completed(void *user, uint16_t handle, uint16_t count)
{
	(void)user;
	int i = pad_by_handle(handle);
	if(i < 0 || pads[i].injected == 0 || count == 0)
		return count;
	uint16_t ours = count < pads[i].injected ? count : (uint16_t)pads[i].injected;
	pads[i].injected -= ours;
	if(pads[i].injected == 0)
	{
		char a[18];
		log_line("chip sent the output reports to %s", addr_str(pads[i].bdaddr, a));
	}
	return (uint16_t)(count - ours);
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
	CKHciFilterCallbacks from = { NULL, on_packet_from_chip, on_device_class, on_completed };
	CKHciFilterCallbacks to = { NULL, on_packet_to_chip, NULL, NULL };
	pthread_mutex_lock(&lock);
	hci_fd = fd;
	ck_hci_filter_init(&from_chip, true, &from);
	ck_hci_filter_init(&to_chip, false, &to);
	lost_sync_logged[0] = lost_sync_logged[1] = false;
	open_shared();
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

// The two output reports for a pad: the first ends the lightbar's startup
// animation, the second lights player LED 1 and the lightbar. Either one
// switches the pad to the full report.
#define SWITCH_REPORTS 2
#define SWITCH_PACKET_SIZE (9 + 1 + CK_DS5_BT_OUTPUT_SIZE)

static size_t switch_packet(int i, bool first, uint8_t out[SWITCH_PACKET_SIZE])
{
	CKDS5Output output;
	memset(&output, 0, sizeof(output));
	output.seq = pads[i].output_seq;
	pads[i].output_seq = (uint8_t)((pads[i].output_seq + 1) & 0x0f);
	if(first)
		output.lightbar_fade_out = true;
	else
	{
		output.set_lightbar = true;
		output.red = LIGHTBAR_RED;
		output.green = LIGHTBAR_GREEN;
		output.blue = LIGHTBAR_BLUE;
		output.set_player_leds = true;
		output.player_leds = CK_DS5_PLAYER_LEDS_1;
	}
	uint8_t hid[1 + CK_DS5_BT_OUTPUT_SIZE];
	hid[0] = 0xa2; // HIDP DATA, output report
	ck_ds5_bt_output_report(hid + 1, &output);
	return ck_hci_acl_frame(out, SWITCH_PACKET_SIZE, pads[i].handle, pads[i].acl_flags, pads[i].interrupt.remote, hid,
		sizeof(hid));
}

// Called after every read from the chip: sends the output reports a pad is
// waiting for, if BTServer is not halfway through writing a packet. When it
// is busy, the next read tries again.
static void switch_pads(void)
{
	if(!atomic_load(&switch_wanted))
		return;
	if(pthread_mutex_trylock(&write_lock) != 0)
		return;
	static uint8_t packets[SWITCH_REPORTS][SWITCH_PACKET_SIZE];
	size_t sizes[SWITCH_REPORTS] = { 0 };
	int fd = -1, i = -1;
	uint16_t handle = 0;
	unsigned attempt = 0;
	pthread_mutex_lock(&lock);
	if(hci_fd >= 0 && ck_hci_filter_between_packets(&to_chip))
	{
		bool more = false;
		for(size_t k = 0; k < pad_count; k++)
		{
			if(!pads[k].connected || !pads[k].want_switch)
				continue;
			if(i >= 0)
			{
				more = true;
				break;
			}
			i = (int)k;
		}
		atomic_store(&switch_wanted, more);
	}
	if(i >= 0)
	{
		fd = hci_fd;
		handle = pads[i].handle;
		for(int r = 0; r < SWITCH_REPORTS; r++)
			sizes[r] = switch_packet(i, r == 0, packets[r]);
		pads[i].want_switch = false;
		pads[i].simple_since_try = 0;
		attempt = ++pads[i].switch_tries;
		pads[i].injected += SWITCH_REPORTS;
	}
	pthread_mutex_unlock(&lock);

	if(i >= 0)
	{
		int sent = 0;
		int error = 0;
		for(int r = 0; r < SWITCH_REPORTS; r++)
		{
			ssize_t w = orig_write(fd, packets[r], sizes[r]);
			if(w != (ssize_t)sizes[r])
			{
				error = w < 0 ? errno : EIO;
				break;
			}
			// The to_chip filter follows the stream, so it sees them too.
			feed(fd, &to_chip, 1, packets[r], w);
			sent++;
		}
		pthread_mutex_lock(&lock);
		if(pads[i].connected && pads[i].handle == handle)
			pads[i].injected -= (unsigned)(SWITCH_REPORTS - sent);
		pthread_mutex_unlock(&lock);
		char a[18];
		if(sent == SWITCH_REPORTS)
			log_line("asked %s for the full report, try %u", addr_str(pads[i].bdaddr, a), attempt);
		else
			log_line("could not ask %s for the full report, try %u: %s", addr_str(pads[i].bdaddr, a), attempt,
				strerror(error));
	}
	pthread_mutex_unlock(&write_lock);
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
	if(fd == hci_fd)
	{
		feed(fd, &from_chip, 0, buf, r);
		switch_pads();
	}
	errno = saved;
	return r;
}

static ssize_t new_write(int fd, const void *buf, size_t n)
{
	if(fd != hci_fd)
		return orig_write(fd, buf, n);
	pthread_mutex_lock(&write_lock);
	ssize_t r = orig_write(fd, buf, n);
	int saved = errno;
	feed(fd, &to_chip, 1, buf, r);
	pthread_mutex_unlock(&write_lock);
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
	if(!(flags & MSG_PEEK) && fd == hci_fd)
	{
		feed(fd, &from_chip, 0, buf, r);
		switch_pads();
	}
	errno = saved;
	return r;
}

// The filter's own packets go out with write(), under write_lock, which keeps
// them apart from BTServer's whether it uses write() or send().
static ssize_t new_send(int fd, const void *buf, size_t n, int flags)
{
	if(fd != hci_fd)
		return orig_send(fd, buf, n, flags);
	pthread_mutex_lock(&write_lock);
	ssize_t r = orig_send(fd, buf, n, flags);
	int saved = errno;
	note_recv_send(fd);
	feed(fd, &to_chip, 1, buf, r);
	pthread_mutex_unlock(&write_lock);
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
