/* SPDX-License-Identifier: BSD-2-Clause */

#define PIC1                0x20     /* IO base address for master PIC */
#define PIC2                0xA0     /* IO base address for slave PIC */
#define PIC1_COMMAND        PIC1
#define PIC1_DATA           (PIC1+1)
#define PIC2_COMMAND        PIC2
#define PIC2_DATA           (PIC2+1)
#define PIC_EOI             0x20     /* End-of-interrupt command code */
#define PIC_READ_IRR        0x0a     /* OCW3 irq ready next CMD read */
#define PIC_READ_ISR        0x0b     /* OCW3 irq service next CMD read */

#define ICW1_ICW4           0x01     /* ICW4 (not) needed */
#define ICW1_SINGLE         0x02     /* Single (cascade) mode */
#define ICW1_INTERVAL4      0x04     /* Call address interval 4 (8) */
#define ICW1_LEVEL          0x08     /* Level triggered (edge) mode */
#define ICW1_INIT           0x10     /* Initialization - required! */

#define ICW4_8086           0x01     /* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO           0x02     /* Auto (normal) EOI */
#define ICW4_BUF_SLAVE      0x08     /* Buffered mode/slave */
#define ICW4_BUF_MASTER     0x0C     /* Buffered mode/master */
#define ICW4_SFNM           0x10     /* Special fully nested (not) */

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

static void pic_remap(u8 offset1, u8 offset2)
{
   u8 a1, a2;

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

static void pic_send_eoi(int irq)
{
   ASSERT(0 <= irq && irq <= 15);

   if (irq >= 8) {
      outb(PIC2_COMMAND, PIC_EOI);
   }

   outb(PIC1_COMMAND, PIC_EOI);
}


static u16 __pic_get_irq_reg(u8 ocw3)
{
   u16 result;

   /* OCW3 to PIC CMD to get the register values.  PIC2 is chained, and
   * represents IRQs 8-15.  PIC1 is IRQs 0-7, with 2 being the chain */
   outb(PIC1_COMMAND, ocw3);
   outb(PIC2_COMMAND, ocw3);

   result = inb(PIC1_COMMAND);
   result |= (u16)(inb(PIC2_COMMAND) << 8);

   return result;
}

/*
 * Returns the combined value of the cascaded PICs irq request register.
 * The Interrupt Request Register (IRR) tells us which interrupts have been
 * raised.
 */
static inline u16 pic_get_irr(void)
{
    return __pic_get_irq_reg(PIC_READ_IRR);
}

/*
 * Returns the combined value of the cascaded PICs in-service register.
 * The In-Service Register (ISR) tells us which interrupts are being serviced,
 * meaning IRQs sent to the CPU.
 */
static inline u16 pic_get_isr(void)
{
    return __pic_get_irq_reg(PIC_READ_ISR);
}

/* IMR = Interrupt Mask Register */
static inline u32 pic_get_imr(void)
{
   return inb(PIC1_DATA) | (u32)(inb(PIC2_DATA) << 8);
}
