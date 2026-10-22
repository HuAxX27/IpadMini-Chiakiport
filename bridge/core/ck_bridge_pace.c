// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "ck_bridge_pace.h"
#include "ck_pad_codec.h"

#include <stdlib.h>
#include <string.h>

void ck_bridge_pace_default_config(CKBridgePaceConfig *config)
{
	config->min_interval_ms = 4;
	config->keepalive_ms = 20;
	config->stick_threshold = 512;
	config->release_count = 3;
}

void ck_bridge_pace_init(CKBridgePace *pace, const CKBridgePaceConfig *config)
{
	memset(pace, 0, sizeof(*pace));
	pace->config = *config;
}

static bool digital_changed(const CKPadPacket *a, const CKPadPacket *b)
{
	return a->flags != b->flags || a->buttons != b->buttons;
}

static bool stick_moved(int16_t a, int16_t b, int32_t threshold)
{
	return abs((int32_t)a - (int32_t)b) >= threshold;
}

static bool analog_changed(const CKPadPacket *a, const CKPadPacket *b, int32_t threshold)
{
	return a->l2 != b->l2
		|| a->r2 != b->r2
		|| stick_moved(a->lx, b->lx, threshold)
		|| stick_moved(a->ly, b->ly, threshold)
		|| stick_moved(a->rx, b->rx, threshold)
		|| stick_moved(a->ry, b->ry, threshold);
}

bool ck_bridge_pace_poll(CKBridgePace *pace, const CKPadPacket *state, uint32_t now_ms, CKPadPacket *out, uint32_t *wait_ms)
{
	const CKBridgePaceConfig *config = &pace->config;
	CKPadPacket neutral;

	if(state)
	{
		pace->pad_present = true;
		pace->releases_left = 0;
	}
	else
	{
		if(pace->pad_present)
		{
			pace->pad_present = false;
			pace->releases_left = config->release_count;
		}
		if(pace->releases_left == 0)
		{
			*wait_ms = CK_BRIDGE_PACE_WAIT_FOREVER;
			return false;
		}
		ck_pad_packet_init(&neutral);
		state = &neutral;
	}

	uint32_t elapsed = now_ms - pace->last_send_ms;
	if(pace->has_sent && !digital_changed(&pace->sent, state) && elapsed < config->keepalive_ms)
	{
		if(!analog_changed(&pace->sent, state, config->stick_threshold))
		{
			*wait_ms = config->keepalive_ms - elapsed;
			return false;
		}
		if(elapsed < config->min_interval_ms)
		{
			*wait_ms = config->min_interval_ms - elapsed;
			return false;
		}
	}

	*out = *state;
	out->magic[0] = CK_PAD_MAGIC_0;
	out->magic[1] = CK_PAD_MAGIC_1;
	out->version = CK_PAD_VERSION;
	out->seq = pace->next_seq++;
	pace->sent = *out;
	pace->has_sent = true;
	pace->last_send_ms = now_ms;

	if(!pace->pad_present && --pace->releases_left == 0)
		*wait_ms = CK_BRIDGE_PACE_WAIT_FOREVER;
	else
		*wait_ms = config->keepalive_ms;
	return true;
}
