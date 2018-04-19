
#include <common/string_util.h>
#include <exos/fault_resumable.h>
#include <exos/hal.h>
#include <exos/process.h>

volatile u32 __fault_resume_mask;
static u32 saved_disable_preemption_count;

/* This cannot be static since asm_do_fault_resumable_call() writes to it */
regs *saved_fault_resumable_regs;

void handle_resumable_fault(regs *r)
{
   ASSERT(!are_interrupts_enabled());
   pop_nested_interrupt(); // the fault
   disable_preemption_count = saved_disable_preemption_count;
   set_return_register(saved_fault_resumable_regs, 1 << regs_intnum(r));
   context_switch(saved_fault_resumable_regs);
}


typedef struct {

   void *func;
   uptr args[6];

} call_ctx;

typedef void (*func0)(void);
typedef void (*func1)(uptr);
typedef void (*func2)(uptr, uptr);
typedef void (*func3)(uptr, uptr, uptr);
typedef void (*func4)(uptr, uptr, uptr, uptr);
typedef void (*func5)(uptr, uptr, uptr, uptr, uptr);
typedef void (*func6)(uptr, uptr, uptr, uptr, uptr, uptr);

static void do_call0(call_ctx *ctx)
{
   ((func0)ctx->func)();
}

static void do_call1(call_ctx *ctx)
{
   ((func1)ctx->func)(ctx->args[0]);
}

static void do_call2(call_ctx *ctx)
{
   ((func2)ctx->func)(ctx->args[0], ctx->args[1]);
}

static void do_call3(call_ctx *ctx)
{
   ((func3)ctx->func)(ctx->args[0], ctx->args[1], ctx->args[2]);
}

static void do_call4(call_ctx *ctx)
{
   ((func4)ctx->func)(ctx->args[0], ctx->args[1], ctx->args[2], ctx->args[3]);
}

static void do_call5(call_ctx *ctx)
{
   ((func5)ctx->func)(ctx->args[0],
                      ctx->args[1], ctx->args[2], ctx->args[3], ctx->args[4]);
}

static void do_call6(call_ctx *ctx)
{
   ((func6)ctx->func)(ctx->args[0], ctx->args[1],
                      ctx->args[2], ctx->args[3], ctx->args[4], ctx->args[5]);
}

static void (*call_wrappers[6])(call_ctx *) =
{
   do_call0, do_call1, do_call2, do_call3, do_call4, do_call5
};

int asm_do_fault_resumable_call(void (*func)(call_ctx *), call_ctx *ctx);

int fault_resumable_call(u32 faults_mask, void *func, u32 nargs, ...)
{
   int r;
   ASSERT(is_preemption_enabled());
   ASSERT(nargs <= 6);

   call_ctx ctx;
   ctx.func = func;

   va_list args;
   va_start(args, nargs);

   for (u32 i = 0; i < nargs; i++)
      ctx.args[i] = va_arg(args, uptr);

   va_end(args);

   disable_preemption();
   {
      __fault_resume_mask = faults_mask;
      saved_disable_preemption_count = disable_preemption_count;
      r = asm_do_fault_resumable_call(call_wrappers[nargs], &ctx);
      __fault_resume_mask = 0;
   }
   enable_preemption();
   return r;
}
