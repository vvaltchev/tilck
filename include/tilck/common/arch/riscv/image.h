/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#ifndef __riscv
   #error This header can be used only when building for riscv.
#endif

/* riscv kernel image header */
struct linux_image_h {
   u32 code0;        /* Executable code */
   u32 code1;        /* Executable code */
   u64 text_offset;  /* Image load offset */
   u64 image_size;   /* Effective Image size */
   u64 flags;        /* kernel flags (little endian) */
   u32 version;      /* version of the header */
   u32 res1;         /* reserved */
   u64 res2;         /* reserved */
   u64 res3;         /* reserved */
   u32 magic;        /* Magic number */
   u32 res4;         /* reserved */
};
