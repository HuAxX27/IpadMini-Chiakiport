// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Linked with the whole core archive, so every object of chiaki-lib must
// resolve against the iOS 9.3 SDK, not only what this file calls.

#include <chiaki/common.h>

#include <stdio.h>

int main(void)
{
	printf("ck-core-check: %s\n", chiaki_error_string(CHIAKI_ERR_SUCCESS));
	return 0;
}
