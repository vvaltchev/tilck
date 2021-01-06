/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Tilck's userspace interface for sound drivers (e.g. /dev/sb16).
 *
 * TODO: drop this by implementing Linux's kernel ALSA interface.
 */

#pragma once
#include <tilck/common/basic_defs.h>

#define TILCK_IOCTL_SOUND_ACQUIRE            1
#define TILCK_IOCTL_SOUND_RELEASE            2
#define TILCK_IOCTL_SOUND_SETUP              3
#define TILCK_IOCTL_SOUND_PAUSE              4
#define TILCK_IOCTL_SOUND_CONTINUE           5
#define TILCK_IOCTL_SOUND_GET_INFO           6
#define TILCK_IOCTL_SOUND_WAIT_COMPLETION    7

/* Used with TILCK_IOCTL_SOUND_GET_INFO */
struct tilck_sound_card_info {

   char name[32];
   u32 max_sample_rate;
   u32 max_bits;
   u32 max_channels;
};

/* Used with TILCK_IOCTL_SOUND_SETUP */
struct tilck_sound_params {

   u16 sample_rate;  /* 44100, 22050 etc.  */
   u8 bits;          /* 8 or 16 */
   u8 channels;      /* 1 or 2 */
   u8 sign;          /* 0 = unsigned, 1 = signed */
};
