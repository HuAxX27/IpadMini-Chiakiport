// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// The app's colors: dark, so the screen does not glare next to a TV and the
// game screen's black edges do not stand out.

#import <UIKit/UIKit.h>

static inline UIColor *CKColor(uint32_t rgb)
{
	return [UIColor colorWithRed:((rgb >> 16) & 0xff) / 255.0 green:((rgb >> 8) & 0xff) / 255.0
		blue:(rgb & 0xff) / 255.0 alpha:1.0];
}

static inline UIColor *CKThemeBackground(void) { return CKColor(0x101215); }
static inline UIColor *CKThemeCell(void) { return CKColor(0x1b1e24); }
static inline UIColor *CKThemeSeparator(void) { return CKColor(0x2c3038); }
static inline UIColor *CKThemeText(void) { return [UIColor whiteColor]; }
static inline UIColor *CKThemeSecondaryText(void) { return CKColor(0x9aa0a8); }
static inline UIColor *CKThemeAccent(void) { return CKColor(0x3d8bff); }
static inline UIColor *CKThemeReady(void) { return CKColor(0x34c759); }
static inline UIColor *CKThemeStandby(void) { return CKColor(0xff9f0a); }
static inline UIColor *CKThemeError(void) { return CKColor(0xff5c5c); }
