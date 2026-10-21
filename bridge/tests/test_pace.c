// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "check.h"
#include "ck_bridge_map.h"
#include "ck_bridge_pace.h"
#include "ck_pad_codec.h"

#include <string.h>

static CKBridgePace pace;
static CKPadPacket out;
static uint32_t wait_ms;

static void setup(void)
{
	CKBridgePaceConfig config;
	ck_bridge_pace_default_config(&config);
	ck_bridge_pace_init(&pace, &config);
	memset(&out, 0, sizeof(out));
}

static CKPadPacket idle(void)
{
	CKPadPacket p;
	ck_pad_packet_init(&p);
	return p;
}

static bool poll(const CKPadPacket *state, uint32_t now)
{
	return ck_bridge_pace_poll(&pace, state, now, &out, &wait_ms);
}

static void test_defaults(void)
{
	CKBridgePaceConfig config;
	ck_bridge_pace_default_config(&config);
	CHECK_EQ(config.min_interval_ms, 4);
	CHECK_EQ(config.keepalive_ms, 20);
	CHECK_EQ(config.stick_threshold, 512);
	CHECK_EQ(config.release_count, 3);
}

static void test_first_send_and_keepalive(void)
{
	setup();
	CKPadPacket s = idle();
	s.seq = 999; // the caller's seq is ignored

	CHECK(poll(&s, 1000));
	CHECK_EQ(out.seq, 0);
	CHECK_EQ(out.magic[0], 'C');
	CHECK_EQ(out.version, CK_PAD_VERSION);
	CHECK_EQ(wait_ms, 20);

	CHECK(!poll(&s, 1005));
	CHECK_EQ(wait_ms, 15);

	CHECK(poll(&s, 1020));
	CHECK_EQ(out.seq, 1);
	CHECK_EQ(wait_ms, 20);
}

static void test_button_goes_out_now(void)
{
	setup();
	CKPadPacket s = idle();
	CHECK(poll(&s, 0));

	s.buttons = CK_PAD_BUTTON_CROSS;
	CHECK(poll(&s, 1));
	CHECK_EQ(out.buttons, CK_PAD_BUTTON_CROSS);

	s.buttons = 0;
	CHECK(poll(&s, 1)); // the release too, even in the same millisecond
	CHECK_EQ(out.buttons, 0);
	CHECK_EQ(out.seq, 2);

	s.flags = CK_PAD_FLAG_TOUCHPAD_PRESSED;
	CHECK(poll(&s, 2));
}

static void test_analog_is_rate_limited(void)
{
	setup();
	CKPadPacket s = idle();
	CHECK(poll(&s, 100));

	s.lx = 10000;
	CHECK(!poll(&s, 102));
	CHECK_EQ(wait_ms, 2);
	CHECK(poll(&s, 104));
	CHECK_EQ(out.lx, 10000);

	s.r2 = 1; // any trigger change counts
	CHECK(!poll(&s, 105));
	CHECK_EQ(wait_ms, 3);
	CHECK(poll(&s, 108));
	CHECK_EQ(out.r2, 1);
}

static void test_stick_jitter_waits_for_keepalive(void)
{
	setup();
	CKPadPacket s = idle();
	CHECK(poll(&s, 0));

	s.ly = 256; // one count of an 8-bit stick
	CHECK(!poll(&s, 5));
	CHECK_EQ(wait_ms, 15);
	s.ly = -255;
	CHECK(!poll(&s, 10));
	CHECK(poll(&s, 20));
	CHECK_EQ(out.ly, -255); // the keepalive carries the latest value

	s.ly = -255 + 512; // a slow drift adds up against the last value sent
	CHECK(poll(&s, 25));
}

static void test_release_after_disconnect(void)
{
	setup();
	CKPadPacket s = idle();
	s.buttons = CK_PAD_BUTTON_R1;
	s.rx = 20000;
	CHECK(poll(&s, 0));

	// Pad gone: the neutral state goes out at once (a button was held), then on each keepalive.
	CHECK(poll(NULL, 3));
	CHECK_EQ(out.buttons, 0);
	CHECK_EQ(out.rx, 0);
	CHECK_EQ(wait_ms, 20);
	CHECK(!poll(NULL, 10));
	CHECK_EQ(wait_ms, 13);
	CHECK(poll(NULL, 23));
	CHECK(poll(NULL, 43));
	CHECK_EQ(out.seq, 3);
	CHECK_EQ(wait_ms, CK_BRIDGE_PACE_WAIT_FOREVER);

	CHECK(!poll(NULL, 63));
	CHECK(!poll(NULL, 10000));
	CHECK_EQ(wait_ms, CK_BRIDGE_PACE_WAIT_FOREVER);

	// A pad that comes back is sent at once and seq keeps counting.
	s.buttons = 0;
	s.rx = 0;
	CHECK(poll(&s, 10001));
	CHECK_EQ(out.seq, 4);
	CHECK_EQ(wait_ms, 20);
}

static void test_nothing_without_a_pad(void)
{
	setup();
	CHECK(!poll(NULL, 0));
	CHECK_EQ(wait_ms, CK_BRIDGE_PACE_WAIT_FOREVER);
}

static void test_wraparounds(void)
{
	setup();
	CKPadPacket s = idle();
	pace.next_seq = 65535;
	CHECK(poll(&s, UINT32_MAX - 9));
	CHECK_EQ(out.seq, 65535);
	CHECK(!poll(&s, 5));
	CHECK_EQ(wait_ms, 5);
	CHECK(poll(&s, 10)); // 20 ms after the last send, across the clock wrap
	CHECK_EQ(out.seq, 0);
}

int main(void)
{
	test_defaults();
	test_first_send_and_keepalive();
	test_button_goes_out_now();
	test_analog_is_rate_limited();
	test_stick_jitter_waits_for_keepalive();
	test_release_after_disconnect();
	test_nothing_without_a_pad();
	test_wraparounds();
	return check_report("test_pace");
}
