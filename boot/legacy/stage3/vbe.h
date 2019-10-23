/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include "realmode_call.h"
#include "mm.h"

/*
 * Struct defines taken from:
 *
 *          VESA BIOS EXTENSION (VBE)
 *             Core Functions
 *                Standard
 *
 *             Version: 3.0
 *         Date: September 16, 1998
 */

struct VbeInfoBlock {

   char VbeSignature[4];
   u16 VbeVersion;
   struct VbeFarPtr OemStringPtr;
   u8 Capabilities[4];
   struct VbeFarPtr VideoModePtr;
   u16 TotalMemory;        /* in number of 64KB blocks */

   /* VBE 2.0+ */
   u16 OemSoftwareRev;
   u32 OemVendorNamePtr;
   u32 OemProductNamePtr;
   u32 OemProductRevPtr;
   u8 reserved[222];
   u8 OemData[256];

} PACKED;

struct ModeInfoBlock {

   u16 ModeAttributes;
   u8 WinAAttributes;
   u8 WinBAttributes;
   u16 WinGranularity;
   u16 WinSize;
   u16 WinASegment;
   u16 WinBSegment;
   struct VbeFarPtr WinFuncPtr;
   u16 BytesPerScanLine;

   /* VBE 1.2+ */

   u16 XResolution;
   u16 YResolution;
   u8 XCharSize;
   u8 YCharSize;
   u8 NumberOfPlanes;
   u8 BitsPerPixel;
   u8 NumberOfBanks;
   u8 MemoryModel;
   u8 BankSize;
   u8 NumberOfImagePages;
   u8 Reserved0;

   /* Direct Color fields (required for direct/6 and YUV/7 memory models) */

   u8 RedMaskSize, RedFieldPosition;
   u8 GreenMaskSize, GreenFieldPosition;
   u8 BlueMaskSize, BlueFieldPosition;
   u8 RsvdMaskSize, RsvdFieldPosition;
   u8 DirectColorModeInfo;

   /* VBE 2.0+ */

   u32 PhysBasePtr;
   u32 Reserved1;
   u16 Reserved2;

   /* VBE 3.0+ */
   u16 LinBytesPerScanLine;

} PACKED;

/*
 * NOTE: the VGA standard modes (0x00 .. 0x13) are reliable, BUT the old
 * VESA 1.2 modes are NOT. Therefore, for the typical VGA text mode, we can just
 * hard-code it, while for the graphical VESA modes, we cannot (we must query
 * the BIOS for the available modes).
 */
#define VGA_COLOR_TEXT_MODE_80x25 (0x03)

#define VBE_MODE_ATTRS_SUPPORTED                       (1 << 0)
#define VBE_MODE_ATTRS_TTY_OUTPUT                      (1 << 2)
#define VBE_MODE_ATTRS_COLOR_MODE                      (1 << 3)
#define VBE_MODE_ATTRS_GFX_MODE                        (1 << 4)
#define VBE_MODE_ATTRS_VGA_COMPAT_WINDOWED_MODE_AVAIL  (1 << 6)
#define VBE_MODE_ATTRS_LINEAR_FB                       (1 << 7)

#define VBE_SET_MODE_FLAG_USE_LINEAR_FB (1 << 14)

#define VB_MEM_MODEL_TEXT_MODE     (0x0)
#define VB_MEM_MODEL_PACKED_PIXEL  (0x4)
#define VB_MEM_MODEL_DIRECT_COLOR  (0x6)

void vga_set_video_mode(u8 mode);
bool vbe_get_info_block(struct VbeInfoBlock *vb);
bool vbe_get_mode_info(u16 mode, struct ModeInfoBlock *mi);
bool vbe_set_video_mode(u16 mode);
bool vbe_get_current_mode(u16 *mode);

/* Implemented in setup_graphics.c */
void ask_user_video_mode(struct mem_info *mi);
