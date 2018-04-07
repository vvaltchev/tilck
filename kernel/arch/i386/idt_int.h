
#pragma once
#include <common/basic_defs.h>


typedef struct
{

   u16 base_lo;
   u16 sel;
   u8 always0;
   u8 flags;
   u16 base_hi;

} PACKED idt_entry;

