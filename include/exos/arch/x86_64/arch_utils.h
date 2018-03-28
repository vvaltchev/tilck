
#pragma once
#include <common/basic_defs.h>

struct regs {
   /* STUB struct */
};

typedef struct regs regs;

static ALWAYS_INLINE int regs_intnum(regs *r)
{
   /* STUB implementation */
   return 0;
}

static ALWAYS_INLINE void set_return_register(regs *r, u32 value)
{
   /* STUB implementation */
}

static ALWAYS_INLINE uptr get_curr_stack_ptr(void)
{
   /* STUB implementation */
   return 0;
}
