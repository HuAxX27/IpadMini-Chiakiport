// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#import <UIKit/UIKit.h>

#import "CKCAppDelegate.h"

#include <chiaki/common.h>

int main(int argc, char *argv[])
{
	@autoreleasepool
	{
		if(chiaki_lib_init() != CHIAKI_ERR_SUCCESS)
			return 1;
		return UIApplicationMain(argc, argv, nil, NSStringFromClass([CKCAppDelegate class]));
	}
}
