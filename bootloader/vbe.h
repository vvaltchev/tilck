
#pragma once
#include "realmode_call.h"

typedef struct {
   char VbeSignature[4];
   u16 VbeVersion;
   far_ptr OemStringPtr;
   u8 Capabilities[4];
   far_ptr VideoModePtr;
   u16 TotalVideoMemory;        // in number of 64KB blocks
} PACKED VbeInfoBlock;

typedef struct {

   u16 attributes;
   u8 winA,winB;
   u16 granularity;
   u16 winsize;
   u16 segmentA, segmentB;
   far_ptr realFctPtr;

   u16 pitch;

   u16 Xres, Yres;
   u8 Wchar, Ychar, planes, bpp, banks;
   u8 memory_model, bank_size, image_pages;
   u8 reserved0;

   u8 red_mask, red_position;
   u8 green_mask, green_position;
   u8 blue_mask, blue_position;
   u8 rsv_mask, rsv_position;
   u8 directcolor_attributes;

   u32 physbase;
   u32 reserved1;
   u16 reserved2;

} PACKED ModeInfoBlock;

void bios_get_vbe_info_block(VbeInfoBlock *vb);
bool bios_get_vbe_info_mode(u16 mode, ModeInfoBlock *mi);
