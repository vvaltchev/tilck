
#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/timer.h>

extern void (*irq_entry_points[16])(void);
static irq_interrupt_handler irq_handlers[16];
static u32 unhandled_irq_count[256];

void idt_set_entry(u8 num, void *handler, u16 sel, u8 flags);

/* This installs a custom IRQ handler for the given IRQ */
void irq_install_handler(u8 irq, irq_interrupt_handler h)
{
   irq_handlers[irq] = h;
   irq_clear_mask(irq);
}

/* This clears the handler for a given IRQ */
void irq_uninstall_handler(u8 irq)
{
   irq_handlers[irq] = NULL;
}

#define PIC1                0x20     /* IO base address for master PIC */
#define PIC2                0xA0     /* IO base address for slave PIC */
#define PIC1_COMMAND        PIC1
#define PIC1_DATA           (PIC1+1)
#define PIC2_COMMAND        PIC2
#define PIC2_DATA           (PIC2+1)
#define PIC_EOI             0x20     /* End-of-interrupt command code */
#define PIC_READ_IRR        0x0a    /* OCW3 irq ready next CMD read */
#define PIC_READ_ISR        0x0b    /* OCW3 irq service next CMD read */

void pic_send_eoi(u8 irq)
{
   if (irq >= 8) {
      outb(PIC2_COMMAND, PIC_EOI);
   }

   outb(PIC1_COMMAND, PIC_EOI);
}


#define ICW1_ICW4 0x01     /* ICW4 (not) needed */
#define ICW1_SINGLE  0x02     /* Single (cascade) mode */
#define ICW1_INTERVAL4  0x04     /* Call address interval 4 (8) */
#define ICW1_LEVEL   0x08     /* Level triggered (edge) mode */
#define ICW1_INIT 0x10     /* Initialization - required! */

#define ICW4_8086 0x01     /* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO 0x02     /* Auto (normal) EOI */
#define ICW4_BUF_SLAVE  0x08     /* Buffered mode/slave */
#define ICW4_BUF_MASTER 0x0C     /* Buffered mode/master */
#define ICW4_SFNM 0x10     /* Special fully nested (not) */

static inline void io_wait() {}



/*
 * Normally, IRQs 0 to 7 are mapped to entries 8 to 15. This
 * is a problem in protected mode, because IDT entry 8 is a
 * Double Fault! Without remapping, every time IRQ0 fires,
 * you get a Double Fault Exception, which is NOT actually
 * what's happening. We send commands to the Programmable
 * Interrupt Controller (PICs - also called the 8259's) in
 * order to make IRQ0 to 15 be remapped to IDT entries 32 to
 * 47.
 */

/*
   arguments:
   offset1 - vector offset for master PIC
   vectors on the master become offset1..offset1+7
   offset2 - same for slave PIC: offset2..offset2+7
*/

void PIC_remap(u8 offset1, u8 offset2)
{
   unsigned char a1, a2;

   a1 = inb(PIC1_DATA);       // save masks
   a2 = inb(PIC2_DATA);

   outb(PIC1_COMMAND, ICW1_INIT + ICW1_ICW4);  // starts the initialization
                                               // sequence (in cascade mode)
   io_wait();
   outb(PIC2_COMMAND, ICW1_INIT + ICW1_ICW4);
   io_wait();
   outb(PIC1_DATA, offset1);                 // ICW2: Master PIC vector offset
   io_wait();
   outb(PIC2_DATA, offset2);                 // ICW2: Slave PIC vector offset
   io_wait();
   outb(PIC1_DATA, 4);                       // ICW3: tell Master PIC that there
                                             // is a slave PIC at IRQ2
                                             // (0000 0100)
   io_wait();
   outb(PIC2_DATA, 2);                       // ICW3: tell Slave PIC its cascade
                                             // identity (0000 0010)
   io_wait();

   outb(PIC1_DATA, ICW4_8086);
   io_wait();
   outb(PIC2_DATA, ICW4_8086);
   io_wait();

   outb(PIC1_DATA, a1);   // restore saved masks.
   outb(PIC2_DATA, a2);
}

void irq_set_mask(u8 irq_line)
{
   u16 port;
   u8 value;

   if (irq_line < 8) {
      port = PIC1_DATA;
   } else {
      port = PIC2_DATA;
      irq_line -= 8;
   }
   value = inb(port) | (1 << irq_line);
   outb(port, value);
}

void irq_clear_mask(u8 irq_line)
{
   u16 port;
   u8 value;

   if (irq_line < 8) {
      port = PIC1_DATA;
   } else {
      port = PIC2_DATA;
      irq_line -= 8;
   }
   value = inb(port) & ~(1 << irq_line);
   outb(port, value);
}


/* Helper func */
static u16 __pic_get_irq_reg(int ocw3)
{
    /* OCW3 to PIC CMD to get the register values.  PIC2 is chained, and
     * represents IRQs 8-15.  PIC1 is IRQs 0-7, with 2 being the chain */
    outb(PIC1_COMMAND, ocw3);
    outb(PIC2_COMMAND, ocw3);
    return (inb(PIC2_COMMAND) << 8) | inb(PIC1_COMMAND);
}

/*
 * Returns the combined value of the cascaded PICs irq request register.
 * The Interrupt Request Register (IRR) tells us which interrupts have been
 * raised.
 */
u16 pic_get_irr(void)
{
    return __pic_get_irq_reg(PIC_READ_IRR);
}

/*
 * Returns the combined value of the cascaded PICs in-service register.
 * The In-Service Register (ISR) tells us which interrupts are being serviced,
 * meaning IRQs sent to the CPU.
 */
u16 pic_get_isr(void)
{
    return __pic_get_irq_reg(PIC_READ_ISR);
}


/* IMR = Interrupt Mask Register */
u32 pic_get_imr(void)
{
   return inb(PIC1_DATA) | inb(PIC2_DATA) << 8;
}



/*
 * We first remap the interrupt controllers, and then we install
 * the appropriate ISRs to the correct entries in the IDT. This
 * is just like installing the exception handlers.
 */

void setup_irq_handling(void)
{
   PIC_remap(32, 40);

   for (int i = 0; i < 16; i++) {
      idt_set_entry(32 + i, irq_entry_points[i], 0x08, 0x8E);
      irq_set_mask(i);
   }
}

static u32 spur_irq_count = 0;
void print_slow_timer_irq_handler_counter(void);

void debug_show_spurious_irq_count(void)
{
   printk(NO_PREFIX "\n");

#if KERNEL_TRACK_NESTED_INTERRUPTS
      print_slow_timer_irq_handler_counter();
#endif

   if (get_ticks() > TIMER_HZ)
      printk("Spur IRQ count: %u (%u/sec)\n",
             spur_irq_count,
             spur_irq_count / (get_ticks() / TIMER_HZ));
   else
      printk("Spurious IRQ count: %u (< 1 sec)\n",
             spur_irq_count, spur_irq_count);

   u32 tot_count = 0;

   for (u32 i = 0; i < ARRAY_SIZE(unhandled_irq_count); i++)
      tot_count += unhandled_irq_count[i];

   if (tot_count) {

      printk("Unhandled IRQs count table\n");

      for (u32 i = 0; i < ARRAY_SIZE(unhandled_irq_count); i++) {

         if (!unhandled_irq_count[i])
            continue;

         printk("IRQ #%3u: %3u unhandled\n", i, unhandled_irq_count[i]);
      }

      printk("\n");
   }
}

static void handle_irq_set_mask(int irq)
{
#if KERNEL_TRACK_NESTED_INTERRUPTS

   /*
    * We can really allow nested IRQ 0 only if we track the nested interrupts,
    * otherwise, the timer handler won't be able to know it's running in a
    * nested way and "bad things may happen".
    */

   if (irq != 0)
      irq_set_mask(irq);

#else
   irq_set_mask(irq);
#endif
}

static void handle_irq_clear_mask(int irq)
{
#if KERNEL_TRACK_NESTED_INTERRUPTS

   if (irq != 0)
      irq_clear_mask(irq);

#else
   irq_clear_mask(irq);
#endif
}

void handle_irq(regs *r)
{
   int handler_ret = 0;
   const u32 irq = r->int_num - 32;

   if (irq == 7 || irq == 15) {

      /*
       * Check for a spurious wake-up.
       *
       * When an IRQ occurs, the PIC chip tells the CPU (via. the PIC's INTR
       * line) that there's an interrupt, and the CPU acknowledges this and
       * waits for the PIC to send the interrupt vector. This creates a race
       * condition: if the IRQ disappears after the PIC has told the CPU there's
       * an interrupt but before the PIC has sent the interrupt vector to the
       * CPU, then the CPU will be waiting for the PIC to tell it which
       * interrupt vector but the PIC won't have a valid interrupt vector to
       * tell the CPU.
       *
       * To get around this, the PIC tells the CPU a fake interrupt number.
       * This is a spurious IRQ. The fake interrupt number is the lowest
       * priority interrupt number for the corresponding PIC chip (IRQ 7 for the
       * master PIC, and IRQ 15 for the slave PIC).
       *
       * Handling Spurious IRQs
       * -------------------------
       *
       * For a spurious IRQ, there is no real IRQ and the PIC chip's ISR
       * (In Service Register) flag for the corresponding IRQ will NOT be set.
       * This means that the interrupt handler must not send an EOI back to the
       * PIC to reset the ISR flag.
       */

      if (!(pic_get_isr() & (1 << irq))) {
         spur_irq_count++;
         return;
      }
   }

   handle_irq_set_mask(irq);
   disable_preemption();
   push_nested_interrupt(r->int_num);

   ASSERT(!are_interrupts_enabled());
   enable_interrupts_forced();

   /*
    * We MUST send EOI to the PIC here, before starting the interrupt handler
    * otherwise, the PIC will just not allow nested interrupts to happen.
    */
   pic_send_eoi(irq);
   ASSERT(are_interrupts_enabled());

   if (irq_handlers[irq]) {
      handler_ret = irq_handlers[irq](r);
   } else {
      unhandled_irq_count[irq]++;
   }

   pop_nested_interrupt();
   enable_preemption();
   handle_irq_clear_mask(irq);

   /////////////////////////////

   if (disable_preemption_count > 0) {
      /*
       * Preemption is disabled: we cannot run the "bottom half" of this
       * interrupt handler right now. The scheduler will run it as soon as
       * possible.
       */
      return;
   }

   if (handler_ret) {
      disable_preemption();
      save_current_task_state(r);

      /*
       * We call here schedule with curr_irq = -1 because we are actually
       * outside the interrupt context (see the pop_nested_interrupt() above()).
       * At the moment, only timer_irq_handler() calls schedule from a proper
       * interrupt context. NOTE: this might change in the future.
       */
      schedule(-1);

      /* In case schedule() returned, we MUST re-enable the preemption */
      enable_preemption();
   }
}


