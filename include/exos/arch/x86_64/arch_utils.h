
#pragma once

#if defined(__EXOS_KERNEL__) && !defined(__EXOS_HAL__)
#error Never include this header directly. Do #include <exos/hal.h>.
#endif

struct regs {
   /* STUB struct */
};

typedef struct regs regs;

static ALWAYS_INLINE int regs_intnum(regs *r)
{
   NOT_IMPLEMENTED();
}

static ALWAYS_INLINE void set_return_register(regs *r, u32 value)
{
   NOT_IMPLEMENTED();
}

static ALWAYS_INLINE uptr get_curr_stack_ptr(void)
{
   NOT_IMPLEMENTED();
}
