#include <commonDefs.h>

#include <irq.h>

#include <stringUtil.h>
#include <term.h>

void idt_set_gate(uint8_t num, void *handler, uint16_t sel, uint8_t flags);


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
void *irq_routines[16] =
{
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0
};

/* This installs a custom IRQ handler for the given IRQ */
void irq_install_handler(uint8_t irq, void(*handler)(regs *r))
{
   irq_routines[irq] = handler;
}

/* This clears the handler for a given IRQ */
void irq_uninstall_handler(uint8_t irq)
{
   irq_routines[irq] = NULL;
}

void irq_remap(void)
{
   outb(0x20, 0x11);
   outb(0xA0, 0x11);
   outb(0x21, 0x20);
   outb(0xA1, 0x28);
   outb(0x21, 0x04);
   outb(0xA1, 0x02);
   outb(0x21, 0x01);
   outb(0xA1, 0x01);
   outb(0x21, 0x0);
   outb(0xA1, 0x0);
}


#define PIC1      0x20     /* IO base address for master PIC */
#define PIC2      0xA0     /* IO base address for slave PIC */
#define PIC1_COMMAND PIC1
#define PIC1_DATA (PIC1+1)
#define PIC2_COMMAND PIC2
#define PIC2_DATA (PIC2+1)

#define PIC_EOI      0x20     /* End-of-interrupt command code */

void PIC_sendEOI(uint8_t irq)
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



/* Normally, IRQs 0 to 7 are mapped to entries 8 to 15. This
*  is a problem in protected mode, because IDT entry 8 is a
*  Double Fault! Without remapping, every time IRQ0 fires,
*  you get a Double Fault Exception, which is NOT actually
*  what's happening. We send commands to the Programmable
*  Interrupt Controller (PICs - also called the 8259's) in
*  order to make IRQ0 to 15 be remapped to IDT entries 32 to
*  47 */

/*
   arguments:
   offset1 - vector offset for master PIC
   vectors on the master become offset1..offset1+7
   offset2 - same for slave PIC: offset2..offset2+7
*/

void PIC_remap(uint8_t offset1, uint8_t offset2)
{
   unsigned char a1, a2;

   a1 = inb(PIC1_DATA);                        // save masks
   a2 = inb(PIC2_DATA);

   outb(PIC1_COMMAND, ICW1_INIT + ICW1_ICW4);  // starts the initialization sequence (in cascade mode)
   io_wait();
   outb(PIC2_COMMAND, ICW1_INIT + ICW1_ICW4);
   io_wait();
   outb(PIC1_DATA, offset1);                 // ICW2: Master PIC vector offset
   io_wait();
   outb(PIC2_DATA, offset2);                 // ICW2: Slave PIC vector offset
   io_wait();
   outb(PIC1_DATA, 4);                       // ICW3: tell Master PIC that there is a slave PIC at IRQ2 (0000 0100)
   io_wait();
   outb(PIC2_DATA, 2);                       // ICW3: tell Slave PIC its cascade identity (0000 0010)
   io_wait();

   outb(PIC1_DATA, ICW4_8086);
   io_wait();
   outb(PIC2_DATA, ICW4_8086);
   io_wait();

   outb(PIC1_DATA, a1);   // restore saved masks.
   outb(PIC2_DATA, a2);
}

void IRQ_set_mask(uint8_t IRQline) {
   uint16_t port;
   uint8_t value;

   if (IRQline < 8) {
      port = PIC1_DATA;
   }
   else {
      port = PIC2_DATA;
      IRQline -= 8;
   }
   value = inb(port) | (1 << IRQline);
   outb(port, value);
}

void IRQ_clear_mask(uint8_t IRQline) {
   uint16_t port;
   uint8_t value;

   if (IRQline < 8) {
      port = PIC1_DATA;
   }
   else {
      port = PIC2_DATA;
      IRQline -= 8;
   }
   value = inb(port) & ~(1 << IRQline);
   outb(port, value);
}


/* We first remap the interrupt controllers, and then we install
*  the appropriate ISRs to the correct entries in the IDT. This
*  is just like installing the exception handlers */
void irq_install()
{
   //irq_remap();
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

/* Each of the IRQ ISRs point to this function, rather than
*  the 'fault_handler' in 'isrs.c'. The IRQ Controllers need
*  to be told when you are done servicing them, so you need
*  to send them an "End of Interrupt" command (0x20). There
*  are two 8259 chips: The first exists at 0x20, the second
*  exists at 0xA0. If the second controller (an IRQ from 8 to
*  15) gets an interrupt, you need to acknowledge the
*  interrupt at BOTH controllers, otherwise, you only send
*  an EOI command to the first controller. If you don't send
*  an EOI, you won't raise any more IRQs */


void irq_handler(regs *r)
{
   /* This is a blank function pointer */
   void(*handler)(regs *r);

   const uint8_t irq_no = r->int_no - 32;

   /* Find out if we have a custom handler to run for this
   *  IRQ, and then finally, run it */
   handler = irq_routines[irq_no];

   if (handler) {

      handler(r);

   } else {

      printk("Unhandled IRQ #%i\n", irq_no);
   }

   PIC_sendEOI(irq_no);
}
