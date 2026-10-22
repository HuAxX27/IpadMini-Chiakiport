// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.7: see ck_video_info.h. The SPS syntax is H.264 clause 7.3.2.1.1.

#include "ck_video_info.h"

#include <string.h>

#define NAL_H264_SEI 6
#define NAL_H264_SPS 7
#define NAL_H264_AUD 9
#define NAL_H265_VPS 32
#define NAL_H265_PPS 34

/** NAL units looked at before giving up: a delimiter and an SEI may come before the SPS. */
#define MAX_LEADING_NALS 4

/** Far above any real picture, so the sizes below never overflow. */
#define MAX_MBS 1024

/** Moves *pos past the next start code. Returns false when there is none. */
static bool skip_start_code(const uint8_t *buf, size_t size, size_t *pos)
{
	for(size_t i = *pos; i + 3 <= size; i++)
	{
		if(buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1)
		{
			*pos = i + 3;
			return true;
		}
	}
	return false;
}

/** Where the NAL unit starting at pos ends: the next start code, its leading zero, or size. */
static size_t nal_end(const uint8_t *buf, size_t size, size_t pos)
{
	size_t end = pos;
	if(!skip_start_code(buf, size, &end))
		return size;
	end -= 3;
	while(end > pos && buf[end - 1] == 0)
		end--;
	return end;
}

// ---------------------------------------------------------------------------
// Bits of an RBSP, dropping the emulation prevention bytes on the way
// ---------------------------------------------------------------------------

typedef struct bits_t
{
	const uint8_t *p;
	const uint8_t *end;
	/** Zero bytes just read, for spotting 00 00 03. */
	unsigned zeros;
	uint8_t byte;
	/** Bits of byte not read yet. */
	unsigned left;
	bool error;
} Bits;

static unsigned read_bit(Bits *b)
{
	if(!b->left)
	{
		if(b->p >= b->end)
		{
			b->error = true;
			return 0;
		}
		uint8_t byte = *b->p++;
		if(b->zeros >= 2 && byte == 3)
		{
			b->zeros = 0;
			if(b->p >= b->end)
			{
				b->error = true;
				return 0;
			}
			byte = *b->p++;
		}
		b->zeros = byte ? 0 : b->zeros + 1;
		b->byte = byte;
		b->left = 8;
	}
	b->left--;
	return (b->byte >> b->left) & 1;
}

static uint32_t read_bits(Bits *b, unsigned count)
{
	uint32_t value = 0;
	while(count--)
		value = (value << 1) | read_bit(b);
	return value;
}

/** ue(v), Exp-Golomb. Codes over 32 bits are errors. */
static uint32_t read_ue(Bits *b)
{
	unsigned zeros = 0;
	while(!read_bit(b))
	{
		if(b->error || ++zeros > 31)
		{
			b->error = true;
			return 0;
		}
	}
	if(!zeros)
		return 0;
	return ((UINT32_C(1) << zeros) - 1) + read_bits(b, zeros);
}

/** se(v). */
static int32_t read_se(Bits *b)
{
	uint32_t code = read_ue(b);
	return (code & 1) ? (int32_t)((code >> 1) + 1) : -(int32_t)(code >> 1);
}

/** ue(v) that must be at most max. */
static uint32_t read_ue_max(Bits *b, uint32_t max)
{
	uint32_t value = read_ue(b);
	if(value > max)
		b->error = true;
	return value;
}

/** scaling_list() of clause 7.3.2.1.1.1, read and thrown away. */
static void skip_scaling_list(Bits *b, unsigned size)
{
	int32_t last = 8, next = 8;
	for(unsigned j = 0; j < size && !b->error; j++)
	{
		if(next)
		{
			int32_t delta = read_se(b);
			if(delta < -128 || delta > 127)
			{
				b->error = true;
				return;
			}
			next = (last + delta + 256) % 256;
		}
		if(next)
			last = next;
	}
}

static bool high_profile(uint8_t profile_idc)
{
	switch(profile_idc)
	{
		case 100: case 110: case 122: case 244: case 44:
		case 83: case 86: case 118: case 128: case 138: case 139: case 134: case 135:
			return true;
		default:
			return false;
	}
}

/** nal is the SPS without its start code, NAL header included. */
static bool parse_sps(const uint8_t *nal, size_t size, CKVideoInfo *info)
{
	Bits b = { nal + 1, nal + size, 0, 0, 0, false };
	info->codec = CK_VIDEO_CODEC_H264;
	info->profile_idc = (uint8_t)read_bits(&b, 8);
	info->constraint_flags = (uint8_t)read_bits(&b, 8);
	info->level_idc = (uint8_t)read_bits(&b, 8);
	read_ue_max(&b, 31); // seq_parameter_set_id

	unsigned chroma_format_idc = 1;
	bool separate_colour_plane = false;
	unsigned bit_depth_luma = 8, bit_depth_chroma = 8;
	if(high_profile(info->profile_idc))
	{
		chroma_format_idc = read_ue_max(&b, 3);
		if(chroma_format_idc == 3)
			separate_colour_plane = read_bit(&b);
		bit_depth_luma = 8 + read_ue_max(&b, 6);
		bit_depth_chroma = 8 + read_ue_max(&b, 6);
		read_bit(&b); // qpprime_y_zero_transform_bypass_flag
		if(read_bit(&b)) // seq_scaling_matrix_present_flag
		{
			unsigned lists = chroma_format_idc != 3 ? 8 : 12;
			for(unsigned i = 0; i < lists && !b.error; i++)
			{
				if(read_bit(&b)) // seq_scaling_list_present_flag
					skip_scaling_list(&b, i < 6 ? 16 : 64);
			}
		}
	}

	read_ue_max(&b, 12); // log2_max_frame_num_minus4
	uint32_t pic_order_cnt_type = read_ue_max(&b, 2);
	if(pic_order_cnt_type == 0)
		read_ue_max(&b, 12); // log2_max_pic_order_cnt_lsb_minus4
	else if(pic_order_cnt_type == 1)
	{
		read_bit(&b); // delta_pic_order_always_zero_flag
		read_se(&b); // offset_for_non_ref_pic
		read_se(&b); // offset_for_top_to_bottom_field
		uint32_t cycle = read_ue_max(&b, 255);
		for(uint32_t i = 0; i < cycle && !b.error; i++)
			read_se(&b); // offset_for_ref_frame
	}
	read_ue(&b); // max_num_ref_frames
	read_bit(&b); // gaps_in_frame_num_value_allowed_flag
	uint32_t width_mbs = read_ue_max(&b, MAX_MBS - 1) + 1;
	uint32_t height_map_units = read_ue_max(&b, MAX_MBS - 1) + 1;
	bool frame_mbs_only = read_bit(&b);
	if(!frame_mbs_only)
		read_bit(&b); // mb_adaptive_frame_field_flag
	read_bit(&b); // direct_8x8_inference_flag
	uint32_t crop_left = 0, crop_right = 0, crop_top = 0, crop_bottom = 0;
	if(read_bit(&b)) // frame_cropping_flag
	{
		crop_left = read_ue_max(&b, MAX_MBS * 16);
		crop_right = read_ue_max(&b, MAX_MBS * 16);
		crop_top = read_ue_max(&b, MAX_MBS * 16);
		crop_bottom = read_ue_max(&b, MAX_MBS * 16);
	}
	if(b.error)
		return false;

	// Clause 7.4.2.1.1: the crop is in chroma samples, and in field pairs when interlaced.
	unsigned frame_height_factor = frame_mbs_only ? 1 : 2;
	uint32_t crop_unit_x = 1, crop_unit_y = frame_height_factor;
	if(!separate_colour_plane && chroma_format_idc != 0)
	{
		crop_unit_x = chroma_format_idc == 3 ? 1 : 2;
		crop_unit_y = (chroma_format_idc == 1 ? 2 : 1) * frame_height_factor;
	}
	uint32_t coded_width = width_mbs * 16;
	uint32_t coded_height = height_map_units * 16 * frame_height_factor;
	uint32_t crop_x = crop_unit_x * (crop_left + crop_right);
	uint32_t crop_y = crop_unit_y * (crop_top + crop_bottom);
	if(crop_x >= coded_width || crop_y >= coded_height)
		return false;

	info->chroma_format_idc = (uint8_t)chroma_format_idc;
	info->bit_depth_luma = (uint8_t)bit_depth_luma;
	info->bit_depth_chroma = (uint8_t)bit_depth_chroma;
	info->interlaced = !frame_mbs_only;
	info->width = coded_width - crop_x;
	info->height = coded_height - crop_y;
	return true;
}

bool ck_video_info_parse(const uint8_t *buf, size_t size, CKVideoInfo *info)
{
	memset(info, 0, sizeof(*info));
	size_t pos = 0;
	for(unsigned i = 0; i < MAX_LEADING_NALS; i++)
	{
		if(!skip_start_code(buf, size, &pos) || pos >= size)
			return false;
		uint8_t first = buf[pos];
		if(first & 0x80) // forbidden_zero_bit
			return false;
		unsigned h264_type = first & 0x1f;
		if(h264_type == NAL_H264_SPS)
		{
			size_t end = nal_end(buf, size, pos);
			if(parse_sps(buf + pos, end - pos, info))
				return true;
			memset(info, 0, sizeof(*info));
			return false;
		}
		// H.265 has a two-byte header: nuh_layer_id 0 and a nonzero nuh_temporal_id_plus1.
		unsigned h265_type = (first >> 1) & 0x3f;
		if(h265_type >= NAL_H265_VPS && h265_type <= NAL_H265_PPS
			&& pos + 1 < size && !(first & 1) && (buf[pos + 1] >> 3) == 0 && (buf[pos + 1] & 7))
		{
			info->codec = CK_VIDEO_CODEC_H265;
			return true;
		}
		if(h264_type != NAL_H264_AUD && h264_type != NAL_H264_SEI)
			return false;
	}
	return false;
}

const char *ck_video_info_profile_name(const CKVideoInfo *info)
{
	if(info->codec != CK_VIDEO_CODEC_H264)
		return "unknown";
	switch(info->profile_idc)
	{
		case 66: return (info->constraint_flags & 0x40) ? "Constrained Baseline" : "Baseline";
		case 77: return "Main";
		case 88: return "Extended";
		case 100: return "High";
		case 110: return "High 10";
		case 122: return "High 4:2:2";
		case 244: return "High 4:4:4 Predictive";
		case 44: return "CAVLC 4:4:4 Intra";
		default: return "unknown";
	}
}

bool ck_video_info_a5_decodes(const CKVideoInfo *info)
{
	return info->codec == CK_VIDEO_CODEC_H264
		&& (info->profile_idc == 66 || info->profile_idc == 77 || info->profile_idc == 100)
		&& info->chroma_format_idc == 1
		&& info->bit_depth_luma == 8 && info->bit_depth_chroma == 8
		&& !info->interlaced
		&& info->level_idc <= 41
		&& info->width <= 1920 && info->height <= 1080;
}
