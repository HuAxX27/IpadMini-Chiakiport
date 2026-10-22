// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "ck_ds5_shared.h"

// A reader tries this many times when the writer keeps changing the report
// under it; at 250 reports per second that is already very unlikely.
#define READ_TRIES 4

bool ck_ds5_shared_valid(const CKDS5Shared *shared)
{
	return shared->magic == CK_DS5_SHARED_MAGIC && shared->version == CK_DS5_SHARED_VERSION;
}

void ck_ds5_shared_attach(CKDS5Shared *shared)
{
	if(!ck_ds5_shared_valid(shared))
	{
		for(size_t i = 0; i < CK_DS5_SHARED_REPORT_MAX; i++)
			atomic_store_explicit(&shared->report[i], 0, memory_order_relaxed);
		atomic_store_explicit(&shared->length, 0, memory_order_relaxed);
		atomic_store_explicit(&shared->seq, 0, memory_order_relaxed);
		shared->version = CK_DS5_SHARED_VERSION;
		shared->magic = CK_DS5_SHARED_MAGIC;
	}
	else if(atomic_load_explicit(&shared->seq, memory_order_relaxed) & 1)
	{
		// A writer died halfway through a report: make the count even again.
		atomic_fetch_add_explicit(&shared->seq, 1, memory_order_release);
	}
	ck_ds5_shared_write(shared, NULL, 0);
}

// The classic sequence lock (Boehm, "Can seqlocks get along with programming
// language memory models?", 2012): relaxed accesses to the report, ordered
// against the sequence number by the fences.
void ck_ds5_shared_write(CKDS5Shared *shared, const uint8_t *report, size_t len)
{
	if(len > CK_DS5_SHARED_REPORT_MAX)
		len = CK_DS5_SHARED_REPORT_MAX;
	uint32_t seq = atomic_load_explicit(&shared->seq, memory_order_relaxed);
	atomic_store_explicit(&shared->seq, seq + 1, memory_order_relaxed);
	atomic_thread_fence(memory_order_release);
	for(size_t i = 0; i < len; i++)
		atomic_store_explicit(&shared->report[i], report[i], memory_order_relaxed);
	atomic_store_explicit(&shared->length, (uint32_t)len, memory_order_relaxed);
	atomic_store_explicit(&shared->seq, seq + 2, memory_order_release);
}

bool ck_ds5_shared_read(const CKDS5Shared *shared, uint32_t *seq, uint8_t *out, size_t *len)
{
	if(!ck_ds5_shared_valid(shared))
		return false;
	for(int i = 0; i < READ_TRIES; i++)
	{
		uint32_t before = atomic_load_explicit(&shared->seq, memory_order_acquire);
		if(before & 1)
			continue;
		if(before == *seq)
			return false;
		uint32_t n = atomic_load_explicit(&shared->length, memory_order_relaxed);
		if(n > CK_DS5_SHARED_REPORT_MAX)
			n = CK_DS5_SHARED_REPORT_MAX;
		for(uint32_t k = 0; k < n; k++)
			out[k] = atomic_load_explicit(&shared->report[k], memory_order_relaxed);
		atomic_thread_fence(memory_order_acquire);
		if(atomic_load_explicit(&shared->seq, memory_order_relaxed) == before)
		{
			*seq = before;
			*len = n;
			return true;
		}
	}
	return false;
}
