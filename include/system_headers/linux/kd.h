/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Shim for <linux/kd.h>.
 * On Linux, forward to the real header. On other platforms, provide the
 * KD (keyboard/display) constants that Tilck needs.
 */

#pragma once

#ifdef __linux__
#include_next <linux/kd.h>
#else

#define KD_TEXT       0x00
#define KD_GRAPHICS   0x01

#define K_XLATE       0x01
#define K_MEDIUMRAW   0x02

#define KB_101        0x02

#define KDSETMODE     0x4B3A
#define KDGETMODE     0x4B3B
#define KDGKBTYPE     0x4B33
#define KDGKBMODE     0x4B44
#define KDSKBMODE     0x4B45

#endif /* !__linux__ */
