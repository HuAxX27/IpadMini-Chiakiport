// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Compile-only check that the plain C contracts build on any host and keep
// their wire layout. CKSession.h is Objective-C and is checked by the iOS
// build once the toolchain (A1) is in place.
//
//   cc -std=c11 -fsyntax-only -Ilib/include -Iios -Ibridge/protocol ios/tests/contracts_check.c

#include "Audio/CKAudioSink.h"
#include "Bridge/CKConsoles.h"
#include "Bridge/CKSettings.h"
#include "Input/CKInputSource.h"
#include "Video/CKVideoSink.h"
#include "ck_pad_packet.h"

#include <stddef.h>

_Static_assert(offsetof(CKPadPacket, buttons) == 6, "buttons offset changed");
_Static_assert(offsetof(CKPadPacket, lx) == 12, "sticks offset changed");
_Static_assert(sizeof(((CKPadPacket *)0)->buttons) == sizeof(((ChiakiControllerState *)0)->buttons),
	"pad buttons must match ChiakiControllerState.buttons");
_Static_assert(sizeof(((CKPadPacket *)0)->lx) == sizeof(((ChiakiControllerState *)0)->left_x),
	"pad sticks must match ChiakiControllerState sticks");

// The sink must stay assignable to the core's callback type.
static ChiakiVideoSampleCallback video_cb_check = ck_video_sink_sample_cb;
