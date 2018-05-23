
#pragma once
#include "realmode_call.h"

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

typedef struct {
   char VbeSignature[4];
   u16 VbeVersion;
   VbeFarPtr OemStringPtr;
   u8 Capabilities[4];
   VbeFarPtr VideoModePtr;
   u16 TotalMemory;        /* in number of 64KB blocks */

   /* VBE 2.0+ */
   u16 OemSoftwareRev;
   u32 OemVendorNamePtr;
   u32 OemProductNamePtr;
   u32 OemProductRevPtr;
   u8 reserved[222];
   u8 OemData[256];

} PACKED VbeInfoBlock;

typedef struct {

   u16 ModeAttributes;
   u8 WinAAttributes;
   u8 WinBAttributes;
   u16 WinGranularity;
   u16 WinSize;
   u16 WinASegment;
   u16 WinBSegment;
   VbeFarPtr WinFuncPtr;
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

} PACKED ModeInfoBlock;

void vbe_get_info_block(VbeInfoBlock *vb);
bool vbe_get_mode_info(u16 mode, ModeInfoBlock *mi);
