// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Linked with the whole core archive (see CMakeLists.txt), so every object of chiaki-lib must
// resolve, not only the ones a test calls. It passes only if the LAN-only core links without curl,
// json-c and miniupnpc, and if CHIAKI_DISABLE_HOLEPUNCH reaches the code that uses the lib.

#include <chiaki/common.h>
#include <chiaki/session.h>

#include <stdio.h>

_Static_assert(CHIAKI_CAN_USE_HOLEPUNCH == 0, "chiaki-lib must export CHIAKI_DISABLE_HOLEPUNCH");
_Static_assert(CHIAKI_CAN_USE_MINIUPNPC == 0, "chiaki-lib must export CHIAKI_DISABLE_HOLEPUNCH");

int main(void)
{
	ChiakiErrorCode err = chiaki_lib_init();
	if(err != CHIAKI_ERR_SUCCESS)
	{
		fprintf(stderr, "chiaki_lib_init failed: %s\n", chiaki_error_string(err));
		return 1;
	}
	printf("LAN-only core linked, sizeof(ChiakiSession) = %u\n", (unsigned)sizeof(ChiakiSession));
	return 0;
}
