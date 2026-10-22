// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Stand-in for Cydia Substrate's header so ckbtfilter.c builds on Linux for
// ios/tests/ckbtfilter_test.c. Nothing gets hooked: each "original" is the
// real function, which is all the test needs to write the log.

#ifndef CK_FAKE_SUBSTRATE_H
#define CK_FAKE_SUBSTRATE_H

static inline void MSHookFunction(void *symbol, void *replacement, void **original)
{
	(void)replacement;
	*original = symbol;
}

#endif
