#include <common_defs.h>

#include <irq.h>
#include <arch/i386/arch_utils.h>

#include <string_util.h>
#include <term.h>
#include <utils.h>
#include <hal.h>

void idt_set_gate(u8 num, void *handler, u16 sel, u8 flags);


/* These are own ISRs that point to our special IRQ handler
*  instead of the regular 'fault_handler' function */
void irq0();
void irq1();
void irq2();
void irq3();
void irq4();
void irq5();
void irq6();
void irq7();
void irq8();
void irq9();
void irq10();
void irq11();
void irq12();
void irq13();
void irq14();
void irq15();

/* This array is actually an array of function pointers. We use
*  this to handle custom IRQ handlers for a given IRQ */
interrupt_handler irq_routines[16] = { NULL };


/* This installs a custom IRQ handler for the given IRQ */
void irq_install_handler(u8 irq, interrupt_handler h)
{
   irq_routines[irq] = h;
}

/* This clears the handler for a given IRQ */
void irq_uninstall_handler(u8 irq)
{
   irq_routines[irq] = NULL;
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

void PIC_sendEOI(u8 irq)
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

void irq_set_mask(u8 IRQline) {
   u16 port;
   u8 value;

   if (IRQline < 8) {
      port = PIC1_DATA;
   } else {
      port = PIC2_DATA;
      IRQline -= 8;
   }
   value = inb(port) | (1 << IRQline);
   outb(port, value);
}

void irq_clear_mask(u8 IRQline) {
   u16 port;
   u8 value;

   if (IRQline < 8) {
      port = PIC1_DATA;
   } else {
      port = PIC2_DATA;
      IRQline -= 8;
   }
   value = inb(port) & ~(1 << IRQline);
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

void irq_install()
{
   PIC_remap(32, 40);

   idt_set_gate(32, irq0, 0x08, 0x8E);
   idt_set_gate(33, irq1, 0x08, 0x8E);
   idt_set_gate(34, irq2, 0x08, 0x8E);
   idt_set_gate(35, irq3, 0x08, 0x8E);
   idt_set_gate(36, irq4, 0x08, 0x8E);
   idt_set_gate(37, irq5, 0x08, 0x8E);
   idt_set_gate(38, irq6, 0x08, 0x8E);
   idt_set_gate(39, irq7, 0x08, 0x8E);
   idt_set_gate(40, irq8, 0x08, 0x8E);
   idt_set_gate(41, irq9, 0x08, 0x8E);
   idt_set_gate(42, irq10, 0x08, 0x8E);
   idt_set_gate(43, irq11, 0x08, 0x8E);
   idt_set_gate(44, irq12, 0x08, 0x8E);
   idt_set_gate(45, irq13, 0x08, 0x8E);
   idt_set_gate(46, irq14, 0x08, 0x8E);
   idt_set_gate(47, irq15, 0x08, 0x8E);
}

void handle_irq(regs *r)
{
   const int irq = r->int_num - 32;
   task_info *curr = get_current_task();

   irq_set_mask(irq);

   if (curr && !curr->running_in_kernel) {
      ASSERT(nested_interrupts_count > 0 || is_preemption_enabled());
   }

   disable_preemption();

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
          //printk("Spurious IRQ #%i\n", irq);
          goto clear_mask_end;
       }
   }

   push_nested_interrupt(r->int_num);

   // Some debug stuff, used for experiments

   // if (nested_interrupts_count > 1) {

   //    if (! (nested_interrupts[nested_interrupts_count - 1] == 32 &&
   //           nested_interrupts[nested_interrupts_count - 2] == 128)) {

   //       printk("(interesting) Nested interrupts: [ ");
   //       for (int i = nested_interrupts_count - 1; i >= 0; i--) {
   //          if (is_irq(nested_interrupts[i])) {
   //             printk("IRQ #%i ", nested_interrupts[i] - 32);
   //          } else if (nested_interrupts[i] == 128) {
   //             printk("(int 0x80) ");
   //          } else {
   //             printk("%i ", nested_interrupts[i]);
   //          }
   //       }
   //       printk("]\n");

   //    }
   // }


   /*
    * Since x86 automatically disables all interrupts before jumping to the
    * interrupt handler, we have to re-enable them manually here.
    */

   ASSERT(!are_interrupts_enabled());
   enable_interrupts_forced();
   ASSERT(are_interrupts_enabled());
   PIC_sendEOI(irq);

   if (irq_routines[irq] != NULL) {

      irq_routines[irq](r);

   } else {

      printk("Unhandled IRQ #%i\n", irq);
   }

   /*
    * We MUST call pop_nested_interrupt() here, BEFORE irq_clear_mask(irq).
    */
   pop_nested_interrupt();

clear_mask_end:
   enable_preemption();

   if (curr && !curr->running_in_kernel) {
      ASSERT(nested_interrupts_count > 0 || is_preemption_enabled());
   }

   irq_clear_mask(irq);
}
