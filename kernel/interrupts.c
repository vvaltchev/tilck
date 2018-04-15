
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <exos/process.h>
#include <exos/hal.h>

void handle_syscall(regs *);
void handle_fault(regs *);
void handle_irq(regs *r);

extern volatile u64 jiffies;

#if KERNEL_TRACK_NESTED_INTERRUPTS

static int nested_interrupts_count;
static int nested_interrupts[MAX_NESTED_INTERRUPTS] =
{
   [0 ... MAX_NESTED_INTERRUPTS-1] = -1
};

inline void push_nested_interrupt(int int_num)
{
   uptr var;
   disable_interrupts(&var);
   {
      ASSERT(nested_interrupts_count < MAX_NESTED_INTERRUPTS);
      ASSERT(nested_interrupts_count >= 0);
      nested_interrupts[nested_interrupts_count++] = int_num;
   }
   enable_interrupts(&var);
}

inline void pop_nested_interrupt(void)
{
   uptr var;
   disable_interrupts(&var);
   {
      nested_interrupts_count--;
      ASSERT(nested_interrupts_count >= 0);
   }
   enable_interrupts(&var);
}

bool in_irq(void)
{
   uptr var;
   bool r = false;
   disable_interrupts(&var);
   {
      if (nested_interrupts_count > 0)
         if (is_irq(nested_interrupts[nested_interrupts_count - 1]))
            r = true;
   }
   enable_interrupts(&var);
   return r;
}

void check_not_in_irq_handler(void)
{
   uptr var;

   if (!in_panic) {
      disable_interrupts(&var);
      {
         if (nested_interrupts_count > 0)
            if (is_irq(nested_interrupts[nested_interrupts_count - 1]))
               panic("NOT expected to be in an IRQ handler");
      }
      enable_interrupts(&var);
   }
}

bool in_syscall(void)
{
   ASSERT(!are_interrupts_enabled());
   bool res = false;

   for (int i = nested_interrupts_count - 1; i >= 0; i--) {
      if (nested_interrupts[i] == SYSCALL_SOFT_INTERRUPT) {
         res = true;
         break;
      }
   }

   return res;
}

static bool is_same_interrupt_nested(int int_num)
{
   ASSERT(!are_interrupts_enabled());

   for (int i = nested_interrupts_count - 1; i >= 0; i--)
      if (nested_interrupts[i] == int_num)
         return true;

   return false;
}


void nested_interrupts_drop_top_syscall(void)
{
   if (nested_interrupts_count > 0) {
      ASSERT(nested_interrupts_count == 1);
      ASSERT(nested_interrupts[0] == SYSCALL_SOFT_INTERRUPT);
      pop_nested_interrupt();
   }
}

void panic_dump_nested_interrupts(void)
{
   VERIFY(in_panic);
   ASSERT(!are_interrupts_enabled());

   if (!nested_interrupts_count)
      return;

   char buf[80];
   int written = 0;

   written += snprintk(buf + written, sizeof(buf) - written, "Interrupts: [ ");

   for (int i = nested_interrupts_count - 1; i >= 0; i--) {
      written += snprintk(buf  + written, sizeof(buf) - written,
                          "%i ", nested_interrupts[i]);
   }

   written += snprintk(buf + written, sizeof(buf) - written, "]\n");
   printk("%s", buf);
}

int get_nested_interrupts_count(void)
{
   ASSERT(!are_interrupts_enabled());
   return nested_interrupts_count;
}

/*
 * This sanity check is essential: it assures us that in no case
 * we're running an usermode thread with preemption disabled.
 */
static ALWAYS_INLINE void DEBUG_check_preemption_enabled_for_usermode(void)
{
   task_info *curr = get_current_task();
   if (curr && !running_in_kernel(curr) && !nested_interrupts_count) {
      ASSERT(is_preemption_enabled());
   }
}

#else

static ALWAYS_INLINE void DEBUG_check_preemption_enabled_for_usermode(void) { }

#endif // KERNEL_TRACK_NESTED_INTERRUPTS


void irq_entry(regs *r)
{
   ASSERT(!are_interrupts_enabled());
   DEBUG_VALIDATE_STACK_PTR();
   DEBUG_check_preemption_enabled_for_usermode();
   ASSERT(get_current_task() != NULL);

#if KERNEL_TRACK_NESTED_INTERRUPTS
   ASSERT(!is_same_interrupt_nested(regs_intnum(r)));
#endif


   handle_irq(r);

   DEBUG_check_preemption_enabled_for_usermode();
}

void soft_interrupt_entry(regs *r)
{
   const int int_num = regs_intnum(r);
   ASSERT(!are_interrupts_enabled());

   DEBUG_check_preemption_enabled_for_usermode();
   push_nested_interrupt(int_num);
   disable_preemption();
   enable_interrupts_forced();
   {
      if (int_num == SYSCALL_SOFT_INTERRUPT)
         handle_syscall(r);
      else
         handle_fault(r);
   }
   disable_interrupts_forced();
   enable_preemption();
   pop_nested_interrupt();
   DEBUG_check_preemption_enabled_for_usermode();
}

