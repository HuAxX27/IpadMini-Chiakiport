// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Tests for the BTServer HCI filter (A5.7). Run with ./ios/tests/input_tests.sh.

#include "ck_hci_filter.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while(0)

#define CHECK_EQ(a, b) do { \
	long long a_ = (long long)(a), b_ = (long long)(b); \
	if(a_ != b_) { \
		fprintf(stderr, "%s:%d: %s == %lld, expected %s == %lld\n", __FILE__, __LINE__, #a, a_, #b, b_); \
		failures++; \
	} \
} while(0)

typedef struct log_t
{
	int packets;
	int inquiries;
	CKHciDeviceClass last_inquiry;
	CKHciPacket last_packet;
} Log;

static void on_packet(void *user, const CKHciPacket *packet)
{
	Log *log = user;
	log->packets++;
	log->last_packet = *packet;
}

static void on_device_class(void *user, const CKHciDeviceClass *device)
{
	Log *log = user;
	log->inquiries++;
	log->last_inquiry = *device;
}

static void init(CKHciFilter *filter, Log *log, bool rewrite)
{
	memset(log, 0, sizeof(*log));
	CKHciFilterCallbacks cb = { log, on_packet, on_device_class };
	ck_hci_filter_init(filter, rewrite, &cb);
}

static const uint8_t pad_addr[6] = { 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 };

// H4 Inquiry Result with RSSI, one response. The class of device starts at byte 12.
#define RSSI_SIZE 18
#define COD_AT 12
static void make_rssi_result(uint8_t *out, uint32_t cod)
{
	uint8_t p[RSSI_SIZE] = { 0x04, 0x22, 15, 1 };
	memcpy(p + 4, pad_addr, 6);
	p[10] = 0x01; // page scan repetition mode R1
	p[11] = 0x00;
	p[12] = cod & 0xff;
	p[13] = (cod >> 8) & 0xff;
	p[14] = (cod >> 16) & 0xff;
	p[15] = 0x34; // clock offset
	p[16] = 0x12;
	p[17] = 0xc8; // RSSI -56
	memcpy(out, p, sizeof(p));
}

// H4 Extended Inquiry Result: 255-byte payload, 240 of them EIR data.
#define EIR_SIZE (3 + 255)
static void make_eir_result(uint8_t *out, uint32_t cod)
{
	memset(out, 0, EIR_SIZE);
	out[0] = 0x04;
	out[1] = 0x2f;
	out[2] = 255;
	out[3] = 1;
	memcpy(out + 4, pad_addr, 6);
	out[10] = 0x01;
	out[12] = cod & 0xff;
	out[13] = (cod >> 8) & 0xff;
	out[14] = (cod >> 16) & 0xff;
	out[17] = 0xc8;
	// EIR: complete local name "DualSense Wireless Controller"
	static const char name[] = "DualSense Wireless Controller";
	out[18] = (uint8_t)(sizeof(name) - 1 + 1);
	out[19] = 0x09;
	memcpy(out + 20, name, sizeof(name) - 1);
}

// H4 Inquiry Result (no RSSI), one response. The class of device starts at byte 13.
#define STANDARD_SIZE 18
#define STANDARD_COD_AT 13
static void make_standard_result(uint8_t *out, uint32_t cod)
{
	uint8_t p[STANDARD_SIZE] = { 0x04, 0x02, 15, 1 };
	memcpy(p + 4, pad_addr, 6);
	p[10] = 0x01; // page scan repetition mode R1
	p[13] = cod & 0xff;
	p[14] = (cod >> 8) & 0xff;
	p[15] = (cod >> 16) & 0xff;
	p[16] = 0x34; // clock offset
	p[17] = 0x12;
	memcpy(out, p, sizeof(p));
}

// H4 Connection Request, as when a paired pad comes back on its own. The class starts at byte 9.
#define REQUEST_SIZE 13
#define REQUEST_COD_AT 9
static void make_connection_request(uint8_t *out, uint32_t cod)
{
	uint8_t p[REQUEST_SIZE] = { 0x04, 0x04, 10 };
	memcpy(p + 3, pad_addr, 6);
	p[9] = cod & 0xff;
	p[10] = (cod >> 8) & 0xff;
	p[11] = (cod >> 16) & 0xff;
	p[12] = 0x01; // ACL link
	memcpy(out, p, sizeof(p));
}

static void test_cod_classes(void)
{
	CHECK(ck_hci_cod_is_gamepad(CK_HCI_COD_DUALSENSE));
	CHECK(ck_hci_cod_is_gamepad(0x002504)); // joystick
	CHECK(ck_hci_cod_is_gamepad(0x000508)); // gamepad, no service bits
	CHECK(!ck_hci_cod_is_gamepad(0x002540)); // keyboard
	CHECK(!ck_hci_cod_is_gamepad(0x002580)); // mouse
	CHECK(!ck_hci_cod_is_gamepad(0x0025c8)); // keyboard and pointer that also claims gamepad
	CHECK(!ck_hci_cod_is_gamepad(0x240404)); // headset
	CHECK(!ck_hci_cod_is_gamepad(0x5a020c)); // phone
	CHECK(!ck_hci_cod_is_gamepad(0x000108)); // computer with the gamepad minor bits
}

static void test_eir_rewritten(void)
{
	CKHciFilter f;
	Log log;
	init(&f, &log, true);
	uint8_t buf[EIR_SIZE], expected[EIR_SIZE];
	make_eir_result(buf, CK_HCI_COD_DUALSENSE);
	memcpy(expected, buf, sizeof(buf));
	expected[COD_AT] = CK_HCI_COD_MINOR_KEYBOARD;

	ck_hci_filter_feed(&f, buf, sizeof(buf));
	CHECK(memcmp(buf, expected, sizeof(buf)) == 0);
	CHECK_EQ(log.packets, 1);
	CHECK_EQ(log.inquiries, 1);
	CHECK(log.last_inquiry.rewritten);
	CHECK(!log.last_inquiry.split);
	CHECK_EQ(log.last_inquiry.cod, CK_HCI_COD_DUALSENSE);
	CHECK(memcmp(log.last_inquiry.bdaddr, pad_addr, 6) == 0);
	CHECK_EQ(log.last_packet.id, CK_HCI_EVENT_EXTENDED_INQUIRY_RESULT);
	CHECK_EQ(log.last_packet.length, 255);
	CHECK_EQ(f.rewrites, 1);
	CHECK(!f.lost_sync);
}

// Every way of cutting the packet in two reads, followed by a second packet to
// show the stream stays in sync. Only a cut right between the minor and the
// major byte stops the rewrite.
static void test_rssi_split_everywhere(void)
{
	for(size_t cut = 1; cut < RSSI_SIZE; cut++)
	{
		CKHciFilter f;
		Log log;
		init(&f, &log, true);
		uint8_t buf[RSSI_SIZE * 2], expected[RSSI_SIZE * 2];
		make_rssi_result(buf, CK_HCI_COD_DUALSENSE);
		make_rssi_result(buf + RSSI_SIZE, CK_HCI_COD_DUALSENSE);
		memcpy(expected, buf, sizeof(buf));
		bool first_split = cut == COD_AT + 1;
		if(!first_split)
			expected[COD_AT] = CK_HCI_COD_MINOR_KEYBOARD;
		expected[RSSI_SIZE + COD_AT] = CK_HCI_COD_MINOR_KEYBOARD;

		ck_hci_filter_feed(&f, buf, cut);
		CHECK_EQ(log.inquiries, 0);
		ck_hci_filter_feed(&f, buf + cut, RSSI_SIZE - cut);
		CHECK_EQ(log.inquiries, 1);
		CHECK_EQ(log.last_inquiry.split, first_split);
		CHECK_EQ(log.last_inquiry.rewritten, !first_split);
		ck_hci_filter_feed(&f, buf + RSSI_SIZE, RSSI_SIZE);
		CHECK_EQ(log.packets, 2);
		CHECK(log.last_inquiry.rewritten);
		if(memcmp(buf, expected, sizeof(buf)) != 0)
		{
			fprintf(stderr, "cut at %zu: wrong bytes\n", cut);
			failures++;
		}
	}
}

static void test_standard_result_rewritten(void)
{
	CKHciFilter f;
	Log log;
	init(&f, &log, true);
	uint8_t buf[STANDARD_SIZE], expected[STANDARD_SIZE];
	make_standard_result(buf, CK_HCI_COD_DUALSENSE);
	memcpy(expected, buf, sizeof(buf));
	expected[STANDARD_COD_AT] = CK_HCI_COD_MINOR_KEYBOARD;
	ck_hci_filter_feed(&f, buf, sizeof(buf));
	CHECK(memcmp(buf, expected, sizeof(buf)) == 0);
	CHECK_EQ(log.inquiries, 1);
	CHECK_EQ(log.last_inquiry.event, CK_HCI_EVENT_INQUIRY_RESULT);
	CHECK_EQ(log.last_inquiry.cod, CK_HCI_COD_DUALSENSE);
	CHECK(log.last_inquiry.rewritten);
	CHECK(memcmp(log.last_inquiry.bdaddr, pad_addr, 6) == 0);
}

static void test_connection_request(void)
{
	// In one read: rewritten, with the right address.
	CKHciFilter f;
	Log log;
	init(&f, &log, true);
	uint8_t buf[REQUEST_SIZE], expected[REQUEST_SIZE];
	make_connection_request(buf, CK_HCI_COD_DUALSENSE);
	memcpy(expected, buf, sizeof(buf));
	expected[REQUEST_COD_AT] = CK_HCI_COD_MINOR_KEYBOARD;
	ck_hci_filter_feed(&f, buf, sizeof(buf));
	CHECK(memcmp(buf, expected, sizeof(buf)) == 0);
	CHECK_EQ(log.inquiries, 1);
	CHECK_EQ(log.last_inquiry.event, CK_HCI_EVENT_CONNECTION_REQUEST);
	CHECK_EQ(log.last_inquiry.cod, CK_HCI_COD_DUALSENSE);
	CHECK(log.last_inquiry.rewritten);
	CHECK(memcmp(log.last_inquiry.bdaddr, pad_addr, 6) == 0);
	CHECK_EQ(log.last_packet.prefix[6], CK_HCI_COD_MINOR_KEYBOARD);

	// Cut between the minor and the major byte: left alone, and said so.
	init(&f, &log, true);
	uint8_t original[REQUEST_SIZE];
	make_connection_request(buf, CK_HCI_COD_DUALSENSE);
	memcpy(original, buf, sizeof(buf));
	ck_hci_filter_feed(&f, buf, REQUEST_COD_AT + 1);
	ck_hci_filter_feed(&f, buf + REQUEST_COD_AT + 1, REQUEST_SIZE - REQUEST_COD_AT - 1);
	CHECK(memcmp(buf, original, sizeof(buf)) == 0);
	CHECK(log.last_inquiry.split);
	CHECK(!log.last_inquiry.rewritten);

	// A keyboard asking to connect is not touched.
	init(&f, &log, true);
	make_connection_request(buf, 0x002540);
	memcpy(original, buf, sizeof(buf));
	ck_hci_filter_feed(&f, buf, sizeof(buf));
	CHECK(memcmp(buf, original, sizeof(buf)) == 0);
	CHECK(!log.last_inquiry.rewritten);
}

// A Connection Complete event is as long as a connection request but carries no class.
static void test_connection_complete_untouched(void)
{
	uint8_t buf[3 + 11] = { 0x04, 0x03, 11, 0x00, 0x0b, 0x00 };
	memcpy(buf + 6, pad_addr, 6);
	buf[12] = 0x01;
	buf[13] = 0x00;
	buf[9] = 0x08; // a byte that would look like a gamepad minor class elsewhere
	uint8_t original[sizeof(buf)];
	memcpy(original, buf, sizeof(buf));
	CKHciFilter f;
	Log log;
	init(&f, &log, true);
	ck_hci_filter_feed(&f, buf, sizeof(buf));
	CHECK(memcmp(buf, original, sizeof(buf)) == 0);
	CHECK_EQ(log.inquiries, 0);
	CHECK_EQ(log.packets, 1);
}

static void test_byte_at_a_time(void)
{
	CKHciFilter f;
	Log log;
	init(&f, &log, true);
	uint8_t buf[RSSI_SIZE], original[RSSI_SIZE];
	make_rssi_result(buf, CK_HCI_COD_DUALSENSE);
	memcpy(original, buf, sizeof(buf));
	for(size_t i = 0; i < sizeof(buf); i++)
		ck_hci_filter_feed(&f, buf + i, 1);
	CHECK(memcmp(buf, original, sizeof(buf)) == 0);
	CHECK_EQ(log.inquiries, 1);
	CHECK(log.last_inquiry.split);
	CHECK(!log.last_inquiry.rewritten);
	CHECK_EQ(f.rewrites, 0);
}

static void test_other_classes_untouched(void)
{
	static const uint32_t classes[] = { 0x002540, 0x002580, 0x240404, 0x5a020c };
	for(size_t i = 0; i < sizeof(classes) / sizeof(classes[0]); i++)
	{
		CKHciFilter f;
		Log log;
		init(&f, &log, true);
		uint8_t buf[RSSI_SIZE], original[RSSI_SIZE];
		make_rssi_result(buf, classes[i]);
		memcpy(original, buf, sizeof(buf));
		ck_hci_filter_feed(&f, buf, sizeof(buf));
		CHECK(memcmp(buf, original, sizeof(buf)) == 0);
		CHECK_EQ(log.last_inquiry.cod, classes[i]);
		CHECK(!log.last_inquiry.rewritten);
	}
}

static void test_several_packets_in_one_read(void)
{
	// Command Complete for Write Inquiry Mode, an ACL packet with 300 bytes, then both kinds of inquiry result.
	static const uint8_t command_complete[] = { 0x04, 0x0e, 0x04, 0x01, 0x45, 0x0c, 0x00 };
	uint8_t acl[5 + 300] = { 0x02, 0x41, 0x20, 300 & 0xff, 300 >> 8 };
	memset(acl + 5, 0x04, 300); // payload full of bytes that look like event starts
	uint8_t stream[sizeof(command_complete) + sizeof(acl) + EIR_SIZE + RSSI_SIZE];
	size_t n = 0;
	memcpy(stream + n, command_complete, sizeof(command_complete));
	n += sizeof(command_complete);
	memcpy(stream + n, acl, sizeof(acl));
	n += sizeof(acl);
	size_t eir_at = n;
	make_eir_result(stream + n, CK_HCI_COD_DUALSENSE);
	n += EIR_SIZE;
	size_t rssi_at = n;
	make_rssi_result(stream + n, CK_HCI_COD_DUALSENSE);
	n += RSSI_SIZE;

	CKHciFilter f;
	Log log;
	init(&f, &log, true);
	ck_hci_filter_feed(&f, stream, n);
	CHECK_EQ(log.packets, 4);
	CHECK_EQ(log.inquiries, 2);
	CHECK_EQ(f.rewrites, 2);
	CHECK_EQ(stream[eir_at + COD_AT], CK_HCI_COD_MINOR_KEYBOARD);
	CHECK_EQ(stream[rssi_at + COD_AT], CK_HCI_COD_MINOR_KEYBOARD);
	for(size_t i = 0; i < 300; i++)
		CHECK_EQ(stream[sizeof(command_complete) + 5 + i], 0x04);
	CHECK(!f.lost_sync);
}

static void test_two_responses_left_alone(void)
{
	CKHciFilter f;
	Log log;
	init(&f, &log, true);
	uint8_t buf[RSSI_SIZE], original[RSSI_SIZE];
	make_rssi_result(buf, CK_HCI_COD_DUALSENSE);
	buf[3] = 2; // num_responses
	memcpy(original, buf, sizeof(buf));
	ck_hci_filter_feed(&f, buf, sizeof(buf));
	CHECK(memcmp(buf, original, sizeof(buf)) == 0);
	CHECK_EQ(log.inquiries, 0);
	CHECK_EQ(log.packets, 1);
}

static void test_observe_only(void)
{
	CKHciFilter f;
	Log log;
	init(&f, &log, false);
	uint8_t buf[RSSI_SIZE], original[RSSI_SIZE];
	make_rssi_result(buf, CK_HCI_COD_DUALSENSE);
	memcpy(original, buf, sizeof(buf));
	ck_hci_filter_feed(&f, buf, sizeof(buf));
	CHECK(memcmp(buf, original, sizeof(buf)) == 0);
	CHECK_EQ(log.inquiries, 1);
	CHECK(!log.last_inquiry.rewritten);
	CHECK(!log.last_inquiry.split);
}

static void test_commands(void)
{
	// Create Connection to the pad: opcode 0x0405, 13 bytes.
	uint8_t cmd[4 + 13] = { 0x01, 0x05, 0x04, 13 };
	memcpy(cmd + 4, pad_addr, 6);
	CKHciFilter f;
	Log log;
	init(&f, &log, false);
	ck_hci_filter_feed(&f, cmd, sizeof(cmd));
	CHECK_EQ(log.packets, 1);
	CHECK_EQ(log.last_packet.type, CK_HCI_COMMAND);
	CHECK_EQ(log.last_packet.id, 0x0405);
	CHECK_EQ(log.last_packet.length, 13);
	CHECK(memcmp(log.last_packet.prefix, pad_addr, 6) == 0);

	// A command with no parameters ends at its header.
	uint8_t reset[] = { 0x01, 0x03, 0x0c, 0x00 };
	ck_hci_filter_feed(&f, reset, sizeof(reset));
	CHECK_EQ(log.packets, 2);
	CHECK_EQ(log.last_packet.id, 0x0c03);
}

static void test_lost_sync(void)
{
	CKHciFilter f;
	Log log;
	init(&f, &log, true);
	uint8_t junk[] = { 0x7f };
	ck_hci_filter_feed(&f, junk, sizeof(junk));
	CHECK(f.lost_sync);
	uint8_t buf[RSSI_SIZE], original[RSSI_SIZE];
	make_rssi_result(buf, CK_HCI_COD_DUALSENSE);
	memcpy(original, buf, sizeof(buf));
	ck_hci_filter_feed(&f, buf, sizeof(buf));
	CHECK(memcmp(buf, original, sizeof(buf)) == 0);
	CHECK_EQ(log.packets, 0);
}

int main(void)
{
	test_cod_classes();
	test_eir_rewritten();
	test_rssi_split_everywhere();
	test_standard_result_rewritten();
	test_connection_request();
	test_connection_complete_untouched();
	test_byte_at_a_time();
	test_other_classes_untouched();
	test_several_packets_in_one_read();
	test_two_responses_left_alone();
	test_observe_only();
	test_commands();
	test_lost_sync();
	if(failures)
	{
		fprintf(stderr, "hci_filter_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("hci_filter_test: ok\n");
	return 0;
}
