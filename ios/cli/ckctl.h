// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// ckctl (A2.4): find, register and wake a console from a shell on the iPad,
// for the spur milestone (P2). main.c only calls ckctl_main(), so the Linux
// tests (ios/tests/core/consoles.c) run the same command lines in-process
// against a fake PS5.

#ifndef CKCTL_H
#define CKCTL_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Runs one ckctl command line. Results go to out, errors and the core's log
 * (with -v) to err. Returns the exit code: 0 done, 1 failed, 2 bad usage.
 */
int ckctl_main(int argc, char **argv, FILE *out, FILE *err);

#ifdef __cplusplus
}
#endif

#endif // CKCTL_H
