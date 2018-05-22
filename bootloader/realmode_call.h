
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
   char VbeSignature[4];        // == "VESA"
   u16 VbeVersion;              // == 0x0300 for VBE 3.0
   u16 OemStringPtr[2];         // isa vbeFarPtr
   u8 Capabilities[4];
   u16 VideoModePtr[2];         // isa vbeFarPtr
   u16 TotalMemory;             // as # of 64KB blocks
} PACKED VbeInfoBlock;


void bios_get_vbe_info_block(VbeInfoBlock *vb);
