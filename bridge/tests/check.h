// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Minimal checks for the bridge tests: report every failure, exit non-zero at the end.

#ifndef BRIDGE_TESTS_CHECK_H
#define BRIDGE_TESTS_CHECK_H

#include <stdio.h>

static int check_failures;

#define CHECK(cond) \
	do \
	{ \
		if(!(cond)) \
		{ \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			check_failures++; \
		} \
	} while(0)

#define CHECK_EQ(a, b) \
	do \
	{ \
		long long check_a_ = (long long)(a), check_b_ = (long long)(b); \
		if(check_a_ != check_b_) \
		{ \
			fprintf(stderr, "%s:%d: CHECK_EQ failed: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, check_a_, check_b_); \
			check_failures++; \
		} \
	} while(0)

static int check_report(const char *name)
{
	if(check_failures)
		fprintf(stderr, "%s: %d failure(s)\n", name, check_failures);
	else
		printf("%s: ok\n", name);
	return check_failures ? 1 : 0;
}

#endif // BRIDGE_TESTS_CHECK_H
