// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Linux test for the BTServer tweak (ios/Input/ckdirect/btfilter, A5.7 and
// A5.8): it builds ckbtfilter.c with a stand-in for Substrate, feeds it the
// HCI traffic of a DualSense pairing and reconnecting, as seen on the iPad on
// 2026-10-22, and checks the log line by line, the reports it shares, the
// output reports it slips in to switch the pad to the full report, and the
// completed-packet counts it hands BTServer. Run it with
// ./ios/tests/input_tests.sh, which passes LOG_PATH and SHARED_PATH.

#ifndef AF_SYSTEM
#define AF_SYSTEM 32 // Darwin's; Linux has no such family
#endif

#include "../Input/ckdirect/btfilter/ckbtfilter.c"

#include <stdlib.h>

#define HCI_FD 100

static int failures = 0;

#define CHECK(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while(0)
static char text[64 * 1024];
static size_t text_len;
static size_t cursor;

static const uint8_t pad_addr[6] = { 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 };
#define PAD "11:22:33:44:55:66"
static const uint8_t keyboard_addr[6] = { 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa };

// What the tweak writes to the HCI socket by itself; everything else goes to write().
#define CAPTURED_MAX 8
static uint8_t captured[CAPTURED_MAX][128];
static size_t captured_len[CAPTURED_MAX];
static int captured_count;

// In the race at the end, everything written to the HCI socket goes here instead.
static uint8_t *stream;
static size_t stream_len, stream_size;

static ssize_t capture_write(int fd, const void *buf, size_t n)
{
	if(fd != HCI_FD)
		return write(fd, buf, n);
	if(stream)
	{
		// Unguarded on purpose: write_lock must already keep writers apart.
		if(stream_len + n <= stream_size)
			memcpy(stream + stream_len, buf, n);
		stream_len += n;
		return (ssize_t)n;
	}
	if(captured_count < CAPTURED_MAX && n <= sizeof(captured[0]))
	{
		memcpy(captured[captured_count], buf, n);
		captured_len[captured_count] = n;
	}
	captured_count++;
	return (ssize_t)n;
}

// One read from the chip, as new_read handles it: through the filter, then a
// chance for the tweak to send what it has waiting. last_read keeps the bytes
// as BTServer gets them.
static uint8_t last_read[512];
static void from(const uint8_t *p, size_t n)
{
	memcpy(last_read, p, n);
	feed(HCI_FD, &from_chip, 0, last_read, (ssize_t)n);
	switch_pads();
}

static void to(const uint8_t *p, size_t n)
{
	uint8_t buf[512];
	memcpy(buf, p, n);
	feed(HCI_FD, &to_chip, 1, buf, (ssize_t)n);
}

static void event(uint8_t code, const uint8_t *payload, uint8_t n)
{
	uint8_t p[3 + 255] = { 0x04, code, n };
	memcpy(p + 3, payload, n);
	from(p, 3u + n);
}

// An ACL packet holding one L2CAP frame; flags 2 for a first fragment, 1 for a continuing one.
static void acl(bool from_chip_side, uint16_t handle, uint8_t flags, uint16_t cid, const uint8_t *data, uint16_t n)
{
	uint8_t p[5 + 4 + 300];
	uint16_t len = (uint16_t)(n + 4);
	p[0] = 0x02;
	p[1] = handle & 0xff;
	p[2] = (uint8_t)((handle >> 8) | (flags << 4));
	p[3] = len & 0xff;
	p[4] = len >> 8;
	p[5] = n & 0xff;
	p[6] = n >> 8;
	p[7] = cid & 0xff;
	p[8] = cid >> 8;
	memcpy(p + 9, data, n);
	if(from_chip_side)
		from(p, 5u + len);
	else
		to(p, 5u + len);
}

static void connection_request_l2cap(bool from_chip_side, uint16_t handle, uint16_t psm, uint16_t scid)
{
	uint8_t s[8] = { 0x02, 0x01, 4, 0, psm & 0xff, psm >> 8, scid & 0xff, scid >> 8 };
	acl(from_chip_side, handle, 2, 0x0001, s, sizeof(s));
}

static void connection_response_l2cap(bool from_chip_side, uint16_t handle, uint16_t dcid, uint16_t scid,
	uint16_t result, uint16_t status)
{
	uint8_t s[12] = { 0x03, 0x01, 8, 0, dcid & 0xff, dcid >> 8, scid & 0xff, scid >> 8, result & 0xff, result >> 8,
		status & 0xff, status >> 8 };
	acl(from_chip_side, handle, 2, 0x0001, s, sizeof(s));
}

static void eir_result(const uint8_t *addr, uint32_t cod)
{
	uint8_t e[255] = { 1 };
	memcpy(e + 1, addr, 6);
	e[7] = 0x01;
	e[9] = cod & 0xff;
	e[10] = (cod >> 8) & 0xff;
	e[11] = (cod >> 16) & 0xff;
	event(0x2f, e, sizeof(e));
}

static void connection_complete(const uint8_t *addr, uint16_t handle)
{
	uint8_t e[11] = { 0x00, handle & 0xff, handle >> 8 };
	memcpy(e + 3, addr, 6);
	e[9] = 0x01; // ACL
	event(0x03, e, sizeof(e));
}

static void completed_packets(uint16_t handle, uint16_t count)
{
	uint8_t e[5] = { 1, handle & 0xff, handle >> 8, count & 0xff, count >> 8 };
	event(0x13, e, sizeof(e));
}

// The count BTServer got in the last Number Of Completed Packets event.
static uint16_t last_completed_count(void)
{
	return le16(last_read + 3 + 3);
}

// One captured packet: an output report 0x31 to the pad's interrupt channel
// on handle, with a good CRC. Returns its player LEDs byte, or -1.
static int check_output(int k, uint16_t handle, uint16_t cid)
{
	const uint8_t *p = captured[k];
	if(captured_len[k] != 9 + 1 + CK_DS5_BT_OUTPUT_SIZE || p[0] != 0x02 || le16(p + 1) != (handle | 0x2000)
		|| le16(p + 3) != 4 + 1 + CK_DS5_BT_OUTPUT_SIZE || le16(p + 5) != 1 + CK_DS5_BT_OUTPUT_SIZE
		|| le16(p + 7) != cid || p[9] != 0xa2 || p[10] != CK_DS5_REPORT_ID_BT_OUTPUT)
	{
		fprintf(stderr, "ckbtfilter_test: captured packet %d is not an output report to 0x%03x\n", k, handle);
		return -1;
	}
	uint32_t crc = ck_ds5_crc32(0, p + 9, 1 + CK_DS5_BT_OUTPUT_SIZE - 4);
	const uint8_t *tail = p + 9 + 1 + CK_DS5_BT_OUTPUT_SIZE - 4;
	if(crc != ((uint32_t)tail[0] | (uint32_t)tail[1] << 8 | (uint32_t)tail[2] << 16 | (uint32_t)tail[3] << 24))
	{
		fprintf(stderr, "ckbtfilter_test: captured packet %d has a bad CRC\n", k);
		return -1;
	}
	return p[10 + 46];
}

static void disconnection_complete(uint16_t handle)
{
	uint8_t e[4] = { 0x00, handle & 0xff, handle >> 8, 0x13 };
	event(0x05, e, sizeof(e));
}

static void read_log(void)
{
	FILE *f = fopen(LOG_PATH, "r");
	if(!f)
	{
		fprintf(stderr, "cannot read %s\n", LOG_PATH);
		exit(1);
	}
	text_len = fread(text, 1, sizeof(text) - 1, f);
	text[text_len] = '\0';
	fclose(f);
}

// The next line that holds needle, after the previous match.
static void expect(const char *needle)
{
	char *at = strstr(text + cursor, needle);
	if(!at)
	{
		fprintf(stderr, "ckbtfilter_test: no line with \"%s\" after offset %zu\n", needle, cursor);
		failures++;
		return;
	}
	char *end = strchr(at, '\n');
	cursor = end ? (size_t)(end - text) : text_len;
}

// The next shared report, if there is one: its length, or -1.
static uint32_t shared_seq;
static uint8_t shared_report[CK_DS5_SHARED_REPORT_MAX];
static int next_shared(void)
{
	size_t len;
	if(!shared || !ck_ds5_shared_read(shared, &shared_seq, shared_report, &len))
		return -1;
	return (int)len;
}

static void expect_absent(const char *needle)
{
	if(strstr(text, needle))
	{
		fprintf(stderr, "ckbtfilter_test: unexpected \"%s\" in the log\n", needle);
		failures++;
	}
}

// BTServer's writer thread sends commands in two writes each, header then
// parameters, while the reader thread keeps wanting to switch the pad. The
// stream that reaches the chip must still be whole packets, and the tweak's
// packets must never land inside one of BTServer's. The writer keeps going
// until the reader is done; the reader gives up after RACE_ATTEMPTS, so a
// broken lock fails the test instead of hanging it.
#define RACE_SWITCHES 1000
#define RACE_ATTEMPTS 1000000
#define RACE_COMMANDS_MAX 200000
#define RACE_COMMAND_SIZE 6

static atomic_bool race_done;
static unsigned race_commands;

static void *race_writer(void *arg)
{
	(void)arg;
	static const uint8_t header[] = { 0x01, 0x05, 0x14, 0x02 }; // Read RSSI, 2 bytes of parameters
	static const uint8_t params[] = { 0x0e, 0x00 };
	while(!atomic_load(&race_done) && race_commands < RACE_COMMANDS_MAX)
	{
		new_write(HCI_FD, header, sizeof(header));
		new_write(HCI_FD, params, sizeof(params));
		race_commands++;
	}
	return NULL;
}

typedef struct race_count_t
{
	unsigned commands;
	unsigned outputs;
} RaceCount;

static void race_count(void *user, const CKHciPacket *p)
{
	RaceCount *c = user;
	if(p->type == CK_HCI_COMMAND && p->id == 0x1405)
		c->commands++;
	else if(p->type == CK_HCI_ACL && p->length == 4 + 1 + CK_DS5_BT_OUTPUT_SIZE && p->prefix[4] == 0xa2)
		c->outputs++;
}

static uint8_t race_buffer[RACE_COMMANDS_MAX * RACE_COMMAND_SIZE + RACE_SWITCHES * SWITCH_REPORTS * SWITCH_PACKET_SIZE];

static void test_race(int pad)
{
	stream_size = sizeof(race_buffer);
	stream = race_buffer;
	atomic_store(&race_done, false);
	pthread_t thread;
	CHECK(pthread_create(&thread, NULL, race_writer, NULL) == 0);
	unsigned asked = 0;
	for(unsigned attempt = 0; asked < RACE_SWITCHES && attempt < RACE_ATTEMPTS; attempt++)
	{
		pthread_mutex_lock(&lock);
		pads[pad].switch_tries = 0;
		pads[pad].want_switch = true;
		pads[pad].injected = 0;
		atomic_store(&switch_wanted, true);
		pthread_mutex_unlock(&lock);
		switch_pads();
		pthread_mutex_lock(&lock);
		if(pads[pad].switch_tries == 1)
			asked++;
		pthread_mutex_unlock(&lock);
	}
	atomic_store(&race_done, true);
	pthread_join(thread, NULL);
	stream = NULL;

	RaceCount count = { 0, 0 };
	CKHciFilterCallbacks cb = { &count, race_count, NULL, NULL };
	CKHciFilter check;
	ck_hci_filter_init(&check, false, &cb);
	CHECK(stream_len <= stream_size);
	ck_hci_filter_feed(&check, race_buffer, stream_len <= stream_size ? stream_len : stream_size);
	CHECK(!check.lost_sync);
	CHECK(ck_hci_filter_between_packets(&check));
	CHECK(asked == RACE_SWITCHES);
	CHECK(count.commands == race_commands);
	CHECK(count.outputs == asked * SWITCH_REPORTS);
	if(failures)
		fprintf(stderr, "ckbtfilter_test: race: %u switches, %u commands; stream had %u and %u outputs\n", asked,
			race_commands, count.commands, count.outputs);
}

int main(void)
{
	orig_write = capture_write;
	start_hci(HCI_FD);
	CHECK(next_shared() == 0); // mapped when BTServer opens the HCI socket, with no pad yet

	// Pairing from Settings: the search finds the pad and a keyboard.
	eir_result(pad_addr, 0x002508);
	eir_result(keyboard_addr, 0x002540);
	connection_complete(pad_addr, 0x00c);
	connection_complete(keyboard_addr, 0x00d);
	CHECK(next_shared() == -1);

	// The iPad opens HID control and interrupt; the pad says "pending", then yes.
	connection_request_l2cap(false, 0x00c, 0x0011, 0x0040);
	connection_response_l2cap(true, 0x00c, 0x0000, 0x0040, 1, 2);
	connection_response_l2cap(true, 0x00c, 0x0041, 0x0040, 0, 0);
	connection_request_l2cap(false, 0x00c, 0x0013, 0x0042);
	connection_response_l2cap(true, 0x00c, 0x0043, 0x0042, 0, 0);

	// BTServer asks for feature report 0x05 on control; the pad answers there.
	static const uint8_t get_report[] = { 0x43, 0x05 };
	acl(false, 0x00c, 2, 0x0041, get_report, sizeof(get_report));
	uint8_t feature[42] = { 0xa3, 0x05 };
	acl(true, 0x00c, 2, 0x0040, feature, sizeof(feature));

	// A simple input report on interrupt, then the same bytes from the keyboard.
	// The pad's report gets two output reports sent to it, on its interrupt
	// channel, with the flags BTServer uses; the keyboard's gets nothing.
	uint8_t simple[11] = { 0xa1, 0x01, 0x80, 0x80, 0x80, 0x80, 0x08 };
	acl(true, 0x00c, 2, 0x0042, simple, sizeof(simple));
	CHECK(next_shared() == 10 && memcmp(shared_report, simple + 1, 10) == 0);
	CHECK(captured_count == 2);
	CHECK(check_output(0, 0x00c, 0x0043) == 0);
	CHECK(captured[0][10 + 44] == 0x02); // lightbar setup: fade out
	CHECK(check_output(1, 0x00c, 0x0043) == CK_DS5_PLAYER_LEDS_1);
	CHECK(captured[1][10 + 1] == 0x10); // the second report's sequence number
	acl(true, 0x00d, 2, 0x0042, simple, sizeof(simple));
	CHECK(next_shared() == -1); // the keyboard's reports are not shared
	CHECK(captured_count == 2);

	// The chip is done with 3 packets on the pad's link, 2 of them ours:
	// BTServer hears of 1. Then of all 3, and of the keyboard's as they are.
	completed_packets(0x00c, 3);
	CHECK(last_completed_count() == 1);
	completed_packets(0x00c, 3);
	CHECK(last_completed_count() == 3);
	completed_packets(0x00d, 2);
	CHECK(last_completed_count() == 2);

	// Still simple: after SWITCH_RETRY_REPORTS more the tweak asks again,
	// but not while BTServer is halfway through writing a packet.
	for(int i = 0; i < SWITCH_RETRY_REPORTS - 1; i++)
		acl(true, 0x00c, 2, 0x0042, simple, sizeof(simple));
	CHECK(captured_count == 2);
	static const uint8_t command_start[] = { 0x01, 0x05, 0x14 }; // Read RSSI, its length still to come
	to(command_start, sizeof(command_start));
	acl(true, 0x00c, 2, 0x0042, simple, sizeof(simple));
	CHECK(captured_count == 2);
	static const uint8_t command_rest[] = { 0x02, 0x0c, 0x00 };
	to(command_rest, sizeof(command_rest));
	completed_packets(0x00d, 1); // any read from the chip will do
	CHECK(captured_count == 4);
	CHECK(check_output(2, 0x00c, 0x0043) == 0);
	CHECK(captured[2][10 + 1] == 0x20); // the sequence goes on
	CHECK(check_output(3, 0x00c, 0x0043) == CK_DS5_PLAYER_LEDS_1);

	// The pad switches; one stray simple report after that asks for nothing.
	// It leaves before the chip counts the second pair, so nothing is held
	// back from that link's counts later.
	uint8_t switched[79] = { 0xa1, 0x31 };
	acl(true, 0x00c, 2, 0x0042, switched, sizeof(switched));
	CHECK(next_shared() == 78);
	acl(true, 0x00c, 2, 0x0042, simple, sizeof(simple));
	CHECK(captured_count == 4);
	disconnection_complete(0x00c);
	CHECK(next_shared() == 0);

	// The pad comes back by itself and opens interrupt; the iPad says "pending", then yes.
	uint8_t request[10];
	memcpy(request, pad_addr, 6);
	request[6] = 0x08;
	request[7] = 0x25;
	request[8] = 0x00;
	request[9] = 0x01;
	event(0x04, request, sizeof(request));
	connection_complete(pad_addr, 0x00b);
	connection_request_l2cap(true, 0x00b, 0x0013, 0x0050);
	connection_response_l2cap(false, 0x00b, 0x0000, 0x0050, 1, 1);
	connection_response_l2cap(false, 0x00b, 0x0060, 0x0050, 0, 0);
	completed_packets(0x00b, 2);
	CHECK(last_completed_count() == 2);
	uint8_t full[79] = { 0xa1, 0x31 };
	full[78] = 0x5a; // the last CRC byte: the whole report must make it through
	acl(true, 0x00b, 2, 0x0060, full, sizeof(full));
	CHECK(next_shared() == 78 && shared_report[0] == 0x31 && shared_report[77] == 0x5a);
	CHECK(captured_count == 4); // already full: nothing to ask
	// A continuing fragment whose bytes look like another report: not counted.
	acl(true, 0x00b, 1, 0x0060, full, sizeof(full));
	disconnection_complete(0x00b);

	read_log();
	expect("ckbtfilter loaded into pid");
	expect("sharing the pad's input reports in");
	expect("inquiry result " PAD ", cod 0x002508: gamepad, now a keyboard");
	expect("inquiry result aa:aa:aa:aa:aa:aa, cod 0x002540\n");
	expect("connection complete " PAD ", status 0x00, handle 0x00c");
	expect("->chip l2cap connection request, psm 0x0011, cid 0x0040");
	expect("chip-> l2cap connection response, cids 0x0000 0x0040, result 1 status 2");
	expect("chip-> l2cap connection response, cids 0x0041 0x0040, result 0 status 0");
	expect("->chip l2cap connection request, psm 0x0013, cid 0x0042");
	expect("hid to " PAD " on control: GET_REPORT 0x43, report 0x05, 2 bytes");
	expect("hid from " PAD " on control: DATA 0xa3, report 0x05, 42 bytes");
	expect("hid input report 0x01 from " PAD " on interrupt, 10 bytes");
	expect("hid to " PAD " on interrupt: DATA 0xa2, report 0x31, 79 bytes");
	expect("hid to " PAD " on interrupt: DATA 0xa2, report 0x31, 79 bytes");
	expect("asked " PAD " for the full report, try 1");
	expect("chip sent the output reports to " PAD);
	expect("asked " PAD " for the full report, try 2");
	expect(PAD " sends the full report now, after 2 requests");
	expect("pad " PAD " gone after 103 input reports");
	expect("connection request from " PAD ", cod 0x002508: gamepad, now a keyboard");
	expect("chip-> l2cap connection request, psm 0x0013, cid 0x0050");
	expect("->chip l2cap connection response, cids 0x0060 0x0050, result 0 status 0");
	expect("hid input report 0x31 from " PAD " on interrupt, 78 bytes");
	expect(PAD " sends the full report now, after 0 requests");
	expect("pad " PAD " gone after 1 input reports");
	expect_absent("from aa:aa:aa:aa:aa:aa");

	// The pad once more, for the race between BTServer's writes and the tweak's.
	connection_complete(pad_addr, 0x00e);
	connection_request_l2cap(false, 0x00e, 0x0013, 0x0044);
	connection_response_l2cap(true, 0x00e, 0x0045, 0x0044, 0, 0);
	int pad = pad_by_handle(0x00e);
	CHECK(pad >= 0 && pads[pad].interrupt.remote == 0x0045);
	if(pad >= 0)
		test_race(pad);

	if(failures)
	{
		fprintf(stderr, "ckbtfilter_test: %d failure(s); the log was:\n%s", failures, text);
		return 1;
	}
	printf("ckbtfilter_test: ok\n");
	return 0;
}
