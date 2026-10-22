// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Linux test for what the BTServer tweak (ios/Input/ckdirect/btfilter) writes
// to its log (A5.7): it builds ckbtfilter.c with a stand-in for Substrate,
// feeds it the HCI traffic of a DualSense pairing and reconnecting, as seen on
// the iPad on 2026-10-22, and checks the log line by line and the reports it
// shares. Run it with ./ios/tests/input_tests.sh, which passes LOG_PATH and
// SHARED_PATH.

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

static void from(const uint8_t *p, size_t n)
{
	uint8_t buf[512];
	memcpy(buf, p, n);
	feed(HCI_FD, &from_chip, 0, buf, (ssize_t)n);
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

int main(void)
{
	start_hci(HCI_FD);

	// Pairing from Settings: the search finds the pad and a keyboard.
	eir_result(pad_addr, 0x002508);
	eir_result(keyboard_addr, 0x002540);
	connection_complete(pad_addr, 0x00c);
	connection_complete(keyboard_addr, 0x00d);
	CHECK(next_shared() == 0); // mapped at connection, with no pad yet

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
	uint8_t simple[11] = { 0xa1, 0x01, 0x80, 0x80, 0x80, 0x80, 0x08 };
	acl(true, 0x00c, 2, 0x0042, simple, sizeof(simple));
	CHECK(next_shared() == 10 && memcmp(shared_report, simple + 1, 10) == 0);
	acl(true, 0x00d, 2, 0x0042, simple, sizeof(simple));
	CHECK(next_shared() == -1); // the keyboard's reports are not shared
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
	uint8_t full[79] = { 0xa1, 0x31 };
	full[78] = 0x5a; // the last CRC byte: the whole report must make it through
	acl(true, 0x00b, 2, 0x0060, full, sizeof(full));
	CHECK(next_shared() == 78 && shared_report[0] == 0x31 && shared_report[77] == 0x5a);
	// A continuing fragment whose bytes look like another report: not counted.
	acl(true, 0x00b, 1, 0x0060, full, sizeof(full));
	disconnection_complete(0x00b);

	read_log();
	expect("ckbtfilter loaded into pid");
	expect("inquiry result " PAD ", cod 0x002508: gamepad, now a keyboard");
	expect("inquiry result aa:aa:aa:aa:aa:aa, cod 0x002540\n");
	expect("connection complete " PAD ", status 0x00, handle 0x00c");
	expect("sharing the pad's input reports in");
	expect("->chip l2cap connection request, psm 0x0011, cid 0x0040");
	expect("chip-> l2cap connection response, cids 0x0000 0x0040, result 1 status 2");
	expect("chip-> l2cap connection response, cids 0x0041 0x0040, result 0 status 0");
	expect("->chip l2cap connection request, psm 0x0013, cid 0x0042");
	expect("hid to " PAD " on control: GET_REPORT 0x43, report 0x05, 2 bytes");
	expect("hid from " PAD " on control: DATA 0xa3, report 0x05, 42 bytes");
	expect("hid input report 0x01 from " PAD " on interrupt, 10 bytes");
	expect("pad " PAD " gone after 1 input reports");
	expect("connection request from " PAD ", cod 0x002508: gamepad, now a keyboard");
	expect("chip-> l2cap connection request, psm 0x0013, cid 0x0050");
	expect("->chip l2cap connection response, cids 0x0060 0x0050, result 0 status 0");
	expect("hid input report 0x31 from " PAD " on interrupt, 78 bytes");
	expect("pad " PAD " gone after 1 input reports");
	expect_absent("from aa:aa:aa:aa:aa:aa");

	if(failures)
	{
		fprintf(stderr, "ckbtfilter_test: %d failure(s); the log was:\n%s", failures, text);
		return 1;
	}
	printf("ckbtfilter_test: ok\n");
	return 0;
}
