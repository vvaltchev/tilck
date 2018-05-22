
#pragma once
#include <common/basic_defs.h>

void
realmode_call(void *func,
              u32 *eax_ref,
              u32 *ebx_ref,
              u32 *ecx_ref,
              u32 *edx_ref,
              u32 *esi_ref,
              u32 *edi_ref);

void
realmode_call_by_val(void *func, u32 a, u32 b, u32 c, u32 d, u32 si, u32 di);

/*
 * Realmode functions
 *
 * Usage: realmode_call(&realmode_func_name, <registers>);
 */
extern u32 realmode_set_video_mode;
extern u32 realmode_write_char;
extern u32 realmode_test_out;
extern u32 realmode_int_10h;


// TEST func
void check_rm_out_regs(void);

typedef struct {
   char VbeSignature[4];
   u16 VbeVersion;
   u16 OemStringPtr[2];         // far ptr
   u8 Capabilities[4];
   u16 VideoModePtr[2];         // far ptr
   u16 TotalVideoMemory;        // in number of 64KB blocks
} PACKED VbeInfoBlock;

typedef struct {

   u16 attributes;
   u8 winA,winB;
   u16 granularity;
   u16 winsize;
   u16 segmentA, segmentB;
   u16 realFctPtr[2]; // far ptr

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
