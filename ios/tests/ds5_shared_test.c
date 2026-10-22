// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Tests for the shared DualSense report (A5.7), including a writer thread and
// a reader thread racing. Run with ./ios/tests/input_tests.sh.

#include "ck_ds5_shared.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while(0)

static CKDS5Shared region;

static void test_basics(void)
{
	memset((void *)&region, 0xee, sizeof(region)); // a new file is zeros, an old one anything
	uint32_t seq = 0;
	uint8_t out[CK_DS5_SHARED_REPORT_MAX];
	size_t len = 99;
	CHECK(!ck_ds5_shared_read(&region, &seq, out, &len)); // not set up yet

	ck_ds5_shared_attach(&region);
	CHECK(ck_ds5_shared_valid(&region));
	CHECK(ck_ds5_shared_read(&region, &seq, out, &len)); // "no pad" is news too
	CHECK(len == 0);
	CHECK(!ck_ds5_shared_read(&region, &seq, out, &len));

	static const uint8_t simple[10] = { 0x01, 0x80, 0x80, 0x80, 0x80, 0x08, 0x00, 0x00, 0x00, 0x00 };
	ck_ds5_shared_write(&region, simple, sizeof(simple));
	CHECK(ck_ds5_shared_read(&region, &seq, out, &len));
	CHECK(len == sizeof(simple));
	CHECK(memcmp(out, simple, sizeof(simple)) == 0);
	CHECK(!ck_ds5_shared_read(&region, &seq, out, &len));

	// Too long: cut to the room there is.
	uint8_t big[CK_DS5_SHARED_REPORT_MAX + 20];
	memset(big, 0x31, sizeof(big));
	ck_ds5_shared_write(&region, big, sizeof(big));
	CHECK(ck_ds5_shared_read(&region, &seq, out, &len));
	CHECK(len == CK_DS5_SHARED_REPORT_MAX);

	// The pad goes away.
	ck_ds5_shared_write(&region, NULL, 0);
	CHECK(ck_ds5_shared_read(&region, &seq, out, &len));
	CHECK(len == 0);
}

static void test_writer_busy_and_restart(void)
{
	ck_ds5_shared_attach(&region);
	uint32_t seq = 0;
	uint8_t out[CK_DS5_SHARED_REPORT_MAX];
	size_t len;
	while(ck_ds5_shared_read(&region, &seq, out, &len))
		;

	// A writer halfway through a report: readers wait for it.
	uint32_t s = atomic_load(&region.seq);
	atomic_store(&region.seq, s + 1);
	CHECK(!ck_ds5_shared_read(&region, &seq, out, &len));

	// BTServer restarts mid-report: the new writer keeps the count going, so
	// a reader that was following along sees the "no pad" right away.
	ck_ds5_shared_attach(&region);
	CHECK(!(atomic_load(&region.seq) & 1));
	CHECK(atomic_load(&region.seq) > seq);
	CHECK(ck_ds5_shared_read(&region, &seq, out, &len));
	CHECK(len == 0);
}

// The writer fills every report with one value and alternates sizes, as the
// pad does when it leaves the simple report; a torn copy would mix values.
#define RACE_REPORTS 200000
static volatile bool writer_done;

static void *writer(void *arg)
{
	(void)arg;
	uint8_t report[CK_DS5_SHARED_REPORT_MAX];
	for(unsigned i = 1; i <= RACE_REPORTS; i++)
	{
		size_t len = (i & 1) ? 10 : 78;
		memset(report, (int)(i & 0xff), len);
		ck_ds5_shared_write(&region, report, len);
	}
	writer_done = true;
	return NULL;
}

static void test_race(void)
{
	ck_ds5_shared_attach(&region);
	writer_done = false;
	pthread_t thread;
	CHECK(pthread_create(&thread, NULL, writer, NULL) == 0);
	uint32_t seq = 0, last = 0;
	uint8_t out[CK_DS5_SHARED_REPORT_MAX];
	size_t len;
	unsigned reads = 0, torn = 0, backwards = 0;
	while(!writer_done || ck_ds5_shared_read(&region, &seq, out, &len))
	{
		if(!ck_ds5_shared_read(&region, &seq, out, &len))
			continue;
		reads++;
		if(seq <= last)
			backwards++;
		last = seq;
		if(len == 0)
			continue;
		if(len != 10 && len != 78)
			torn++;
		for(size_t i = 1; i < len; i++)
			if(out[i] != out[0])
			{
				torn++;
				break;
			}
	}
	pthread_join(thread, NULL);
	CHECK(torn == 0);
	CHECK(backwards == 0);
	CHECK(reads > 0);
	// The last report the writer made is the one left.
	CHECK(atomic_load(&region.seq) == seq);
	CHECK(atomic_load(&region.length) == 78 && atomic_load(&region.report[0]) == (RACE_REPORTS & 0xff));
	if(torn)
		fprintf(stderr, "ds5_shared_test: %u torn reads in %u\n", torn, reads);
}

int main(void)
{
	test_basics();
	test_writer_busy_and_restart();
	test_race();
	if(failures)
	{
		fprintf(stderr, "ds5_shared_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("ds5_shared_test: ok\n");
	return 0;
}
