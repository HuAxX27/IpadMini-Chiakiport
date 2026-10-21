// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "check.h"
#include "ck_pad_codec.h"

#include <string.h>

static void test_init(void)
{
	CKPadPacket p;
	memset(&p, 0xAA, sizeof(p));
	ck_pad_packet_init(&p);
	CHECK_EQ(p.magic[0], 'C');
	CHECK_EQ(p.magic[1], 'K');
	CHECK_EQ(p.version, 1);
	CHECK_EQ(p.flags, 0);
	CHECK_EQ(p.seq, 0);
	CHECK_EQ(p.buttons, 0);
	CHECK_EQ(p.l2, 0);
	CHECK_EQ(p.ry, 0);
}

static CKPadPacket sample(void)
{
	CKPadPacket p;
	ck_pad_packet_init(&p);
	p.flags = 0x01;
	p.seq = 0xBEEF;
	p.buttons = 0x0000C001; // cross, touchpad, PS
	p.l2 = 0x12;
	p.r2 = 0xFF;
	p.lx = -32768;
	p.ly = 32767;
	p.rx = -2;
	p.ry = 0x0102;
	return p;
}

static void test_wire_layout(void)
{
	static const uint8_t expected[CK_PAD_PACKET_SIZE] = {
		'C', 'K', 0x01, 0x01,   // magic, version, flags
		0xEF, 0xBE,             // seq
		0x01, 0xC0, 0x00, 0x00, // buttons
		0x12, 0xFF,             // l2, r2
		0x00, 0x80,             // lx
		0xFF, 0x7F,             // ly
		0xFE, 0xFF,             // rx
		0x02, 0x01,             // ry
	};
	CKPadPacket p = sample();
	uint8_t buf[CK_PAD_PACKET_SIZE];
	ck_pad_packet_write(&p, buf);
	CHECK(memcmp(buf, expected, sizeof(expected)) == 0);

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	// The contract sends the packed struct as is on little-endian hosts: both ways must agree.
	CHECK(memcmp(buf, &p, sizeof(p)) == 0);
#endif
}

static void test_round_trip(void)
{
	CKPadPacket p = sample();
	CKPadPacket q;
	uint8_t buf[CK_PAD_PACKET_SIZE];
	ck_pad_packet_write(&p, buf);
	memset(&q, 0, sizeof(q));
	CHECK(ck_pad_packet_read(&q, buf, sizeof(buf)));
	CHECK(memcmp(&p, &q, sizeof(p)) == 0);
	CHECK(ck_pad_packet_same_state(&p, &q));
}

static void test_read_rejects(void)
{
	CKPadPacket p = sample();
	CKPadPacket q;
	uint8_t buf[CK_PAD_PACKET_SIZE + 1];
	ck_pad_packet_write(&p, buf);
	buf[CK_PAD_PACKET_SIZE] = 0;

	CHECK(!ck_pad_packet_read(&q, buf, CK_PAD_PACKET_SIZE - 1));
	CHECK(!ck_pad_packet_read(&q, buf, CK_PAD_PACKET_SIZE + 1));
	CHECK(!ck_pad_packet_read(&q, buf, 0));

	buf[0] = 'X';
	CHECK(!ck_pad_packet_read(&q, buf, CK_PAD_PACKET_SIZE));
	buf[0] = 'C';
	buf[1] = 'X';
	CHECK(!ck_pad_packet_read(&q, buf, CK_PAD_PACKET_SIZE));
	buf[1] = 'K';
	buf[2] = 2;
	CHECK(!ck_pad_packet_read(&q, buf, CK_PAD_PACKET_SIZE));
	buf[2] = 1;
	CHECK(ck_pad_packet_read(&q, buf, CK_PAD_PACKET_SIZE));
}

static void test_same_state_ignores_seq(void)
{
	CKPadPacket a = sample();
	CKPadPacket b = a;
	b.seq++;
	CHECK(ck_pad_packet_same_state(&a, &b));
	b.ry++;
	CHECK(!ck_pad_packet_same_state(&a, &b));
	b = a;
	b.flags = 0;
	CHECK(!ck_pad_packet_same_state(&a, &b));
}

static void test_seq_newer(void)
{
	CHECK(ck_pad_seq_newer(1, 0));
	CHECK(!ck_pad_seq_newer(0, 0));
	CHECK(!ck_pad_seq_newer(0, 1));
	CHECK(ck_pad_seq_newer(0, 65535));
	CHECK(ck_pad_seq_newer(5, 65530));
	CHECK(ck_pad_seq_newer(32767, 0));
	CHECK(!ck_pad_seq_newer(32768, 0));
}

static void test_rx_filter(void)
{
	CKPadRxFilter f;
	ck_pad_rx_filter_init(&f);

	CHECK(ck_pad_rx_filter_accept(&f, 1000, 0)); // first packet, any seq
	CHECK(!ck_pad_rx_filter_accept(&f, 1000, 1)); // duplicate
	CHECK(!ck_pad_rx_filter_accept(&f, 999, 2));  // late
	CHECK(ck_pad_rx_filter_accept(&f, 1002, 3));  // a lost packet in between is fine
	CHECK(!ck_pad_rx_filter_accept(&f, 1001, 4)); // arrived after a newer one

	// The bridge rebooted: seq starts over and is taken once the receiver has heard nothing for a while.
	CHECK(!ck_pad_rx_filter_accept(&f, 0, 4 + CK_PAD_RESYNC_MS - 2));
	CHECK(ck_pad_rx_filter_accept(&f, 0, 3 + CK_PAD_RESYNC_MS));
	CHECK(ck_pad_rx_filter_accept(&f, 1, 4 + CK_PAD_RESYNC_MS));

	// Wraparound.
	ck_pad_rx_filter_init(&f);
	CHECK(ck_pad_rx_filter_accept(&f, 65535, 10));
	CHECK(ck_pad_rx_filter_accept(&f, 0, 11));

	// The millisecond clock wraps too.
	ck_pad_rx_filter_init(&f);
	CHECK(ck_pad_rx_filter_accept(&f, 10, UINT32_MAX - 5));
	CHECK(!ck_pad_rx_filter_accept(&f, 9, 4));
	CHECK(ck_pad_rx_filter_accept(&f, 9, CK_PAD_RESYNC_MS));
}

int main(void)
{
	test_init();
	test_wire_layout();
	test_round_trip();
	test_read_rejects();
	test_same_state_ignores_seq();
	test_seq_newer();
	test_rx_filter();
	return check_report("test_codec");
}
