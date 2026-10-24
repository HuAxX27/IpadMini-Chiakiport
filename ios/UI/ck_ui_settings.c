// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A6.4: the settings the app offers (ck_ui_settings.h).

#include "ck_ui_settings.h"

#include <stdio.h>

const ChiakiVideoResolutionPreset ck_ui_resolutions[CK_UI_RESOLUTION_COUNT] = {
	CHIAKI_VIDEO_RESOLUTION_PRESET_360p,
	CHIAKI_VIDEO_RESOLUTION_PRESET_540p,
	CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
};

const ChiakiVideoFPSPreset ck_ui_fps[CK_UI_FPS_COUNT] = {
	CHIAKI_VIDEO_FPS_PRESET_30,
	CHIAKI_VIDEO_FPS_PRESET_60,
};

const uint32_t ck_ui_bitrates_kbps[CK_UI_BITRATE_COUNT] = { 0, 2000, 4000, 6000, 8000, 10000, 15000 };

static const char *const bitrate_labels[CK_UI_BITRATE_COUNT] = { "Auto", "2", "4", "6", "8", "10", "15" };

unsigned ck_ui_resolution_height(ChiakiVideoResolutionPreset resolution)
{
	switch(resolution)
	{
		case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
			return 360;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
			return 540;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
			return 720;
		default:
			return 0;
	}
}

uint32_t ck_ui_auto_bitrate_kbps(ChiakiVideoResolutionPreset resolution)
{
	// The same as chiaki_connect_video_profile_preset() in lib/src/session.c.
	switch(resolution)
	{
		case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
			return 2000;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
			return 6000;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
			return 10000;
		default:
			return 0;
	}
}

int ck_ui_resolution_index(ChiakiVideoResolutionPreset resolution)
{
	for(int i = 0; i < CK_UI_RESOLUTION_COUNT; i++)
		if(ck_ui_resolutions[i] == resolution)
			return i;
	return -1;
}

int ck_ui_fps_index(ChiakiVideoFPSPreset fps)
{
	for(int i = 0; i < CK_UI_FPS_COUNT; i++)
		if(ck_ui_fps[i] == fps)
			return i;
	return -1;
}

int ck_ui_bitrate_index(uint32_t bitrate_kbps)
{
	for(int i = 0; i < CK_UI_BITRATE_COUNT; i++)
		if(ck_ui_bitrates_kbps[i] == bitrate_kbps)
			return i;
	return -1;
}

const char *ck_ui_resolution_label(ChiakiVideoResolutionPreset resolution)
{
	switch(resolution)
	{
		case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
			return "360p";
		case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
			return "540p";
		case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
			return "720p";
		default:
			return "?";
	}
}

const char *ck_ui_fps_label(ChiakiVideoFPSPreset fps)
{
	switch(fps)
	{
		case CHIAKI_VIDEO_FPS_PRESET_30:
			return "30";
		case CHIAKI_VIDEO_FPS_PRESET_60:
			return "60";
		default:
			return "?";
	}
}

const char *ck_ui_bitrate_label(uint32_t bitrate_kbps)
{
	int i = ck_ui_bitrate_index(bitrate_kbps);
	return i < 0 ? "?" : bitrate_labels[i];
}

static bool bitrate_valid(int64_t kbps)
{
	return kbps == 0 || (kbps >= CK_UI_BITRATE_MIN_KBPS && kbps <= CK_UI_BITRATE_MAX_KBPS);
}

bool ck_ui_settings_from_values(int64_t height, int64_t fps, int64_t bitrate_kbps, CKSettings *settings)
{
	ck_settings_set_default(settings);
	bool valid = true;

	bool found = false;
	for(int i = 0; i < CK_UI_RESOLUTION_COUNT; i++)
		if(height == (int64_t)ck_ui_resolution_height(ck_ui_resolutions[i]))
		{
			settings->resolution = ck_ui_resolutions[i];
			found = true;
		}
	valid = valid && found;

	found = false;
	for(int i = 0; i < CK_UI_FPS_COUNT; i++)
		if(fps == (int64_t)ck_ui_fps[i])
		{
			settings->fps = ck_ui_fps[i];
			found = true;
		}
	valid = valid && found;

	if(bitrate_valid(bitrate_kbps))
		settings->bitrate_kbps = (uint32_t)bitrate_kbps;
	else
		valid = false;
	return valid;
}

void ck_ui_settings_to_values(const CKSettings *settings, int64_t *height, int64_t *fps, int64_t *bitrate_kbps)
{
	*height = ck_ui_resolution_height(settings->resolution);
	*fps = (int64_t)settings->fps;
	*bitrate_kbps = settings->bitrate_kbps;
}

bool ck_ui_settings_valid(const CKSettings *settings)
{
	return ck_ui_resolution_index(settings->resolution) >= 0 && ck_ui_fps_index(settings->fps) >= 0
		&& bitrate_valid(settings->bitrate_kbps);
}

/** "6", "7,5": kbps as Mbps with a decimal comma when needed. */
static void format_mbps(uint32_t kbps, char *out, size_t out_size)
{
	unsigned whole = kbps / 1000, tenths = (kbps % 1000 + 50) / 100;
	if(tenths == 10)
	{
		whole++;
		tenths = 0;
	}
	if(tenths)
		snprintf(out, out_size, "%u,%u", whole, tenths);
	else
		snprintf(out, out_size, "%u", whole);
}

void ck_ui_settings_summary(const CKSettings *settings, char *out, size_t out_size)
{
	if(!out_size)
		return;
	char mbps[24];
	if(settings->bitrate_kbps)
	{
		format_mbps(settings->bitrate_kbps, mbps, sizeof(mbps));
		snprintf(out, out_size, "%s · %s fps · %s Mbps", ck_ui_resolution_label(settings->resolution),
			ck_ui_fps_label(settings->fps), mbps);
	}
	else
	{
		format_mbps(ck_ui_auto_bitrate_kbps(settings->resolution), mbps, sizeof(mbps));
		snprintf(out, out_size, "%s · %s fps · %s Mbps (automático)", ck_ui_resolution_label(settings->resolution),
			ck_ui_fps_label(settings->fps), mbps);
	}
}
