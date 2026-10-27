// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A4.3: see audio_click.h.

#include "audio_click.h"

#include <math.h>

static double d2(const int16_t *pcm, size_t n, uint32_t channels, uint32_t c)
{
	return fabs((double)pcm[n * channels + c] - 2.0 * pcm[(n - 1) * channels + c] + pcm[(n - 2) * channels + c]);
}

double click_max_d2(const int16_t *pcm, size_t frames, uint32_t channels)
{
	double worst = 0.0;
	for(size_t n = 2; n < frames; n++)
		for(uint32_t c = 0; c < channels; c++)
		{
			double v = d2(pcm, n, channels, c);
			if(v > worst)
				worst = v;
		}
	return worst;
}

uint32_t click_count(const int16_t *pcm, size_t frames, uint32_t channels, uint32_t rate, double limit, size_t *first)
{
	uint32_t count = 0;
	size_t quiet_until = 0;
	for(size_t n = 2; n < frames; n++)
	{
		if(n < quiet_until)
			continue;
		for(uint32_t c = 0; c < channels; c++)
		{
			if(d2(pcm, n, channels, c) <= limit)
				continue;
			if(!count && first)
				*first = n;
			count++;
			quiet_until = n + rate / 1000;
			break;
		}
	}
	return count;
}
