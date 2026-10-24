// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "ckctl.h"

#include <chiaki/common.h>

int main(int argc, char **argv)
{
	ChiakiErrorCode err = chiaki_lib_init();
	if(err != CHIAKI_ERR_SUCCESS)
	{
		fprintf(stderr, "ckctl: no se pudo iniciar el core: %s\n", chiaki_error_string(err));
		return 1;
	}
	return ckctl_main(argc, argv, stdout, stderr);
}
