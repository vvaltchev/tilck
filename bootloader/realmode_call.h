
#pragma once
#include <common/basic_defs.h>

void
realmode_call(void *func,
              u32 *eax_ref,
              u32 *ebx_ref,
              u32 *ecx_ref,
              u32 *edx_ref,
              u32 *esi_ref,
              u32 *edi_ref,
              u32 *flags_ref);

void
realmode_call_by_val(void *func, u32 a, u32 b, u32 c, u32 d, u32 si, u32 di);

/*
 * Realmode functions
 *
 * Usage: realmode_call(&realmode_func_name, <registers>);
 */
extern u32 realmode_int_10h;
extern u32 realmode_int_13h;
extern u32 realmode_int_16h;

void test_rm_call_working(void);

typedef struct {

   u16 off;
   u16 seg;

} PACKED VbeFarPtr;

static ALWAYS_INLINE void *get_flat_ptr(VbeFarPtr fp)
{
   return (void *)((u32)fp.off + ((u32)fp.seg) * 16);
}

char bios_read_char(void);

bool read_drive_params(u8 drive,
                       u32 *sectors_per_track,
                       u32 *heads_per_cylinder,
                       u32 *cylinder_count);

void read_sectors(void);
