

#include <commonDefs.h>
#include <stringUtil.h>
#include <term.h>
#include <irq.h>

extern void idt_install();


/* KBDUS means US Keyboard Layout. This is a scancode table
*  used to layout a standard US keyboard. I have left some
*  comments in to give you an idea of what key is what, even
*  though I set it's array index to 0. You can change that to
*  whatever you want using a macro, if you wish! */
unsigned char kbdus[128] =
{
   0,  27, '1', '2', '3', '4', '5', '6', '7', '8',	/* 9 */
   '9', '0', '-', '=', '\b',	/* Backspace */
   '\t',			/* Tab */
   'q', 'w', 'e', 'r',	/* 19 */
   't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',	/* Enter key */
   0,			/* 29   - Control */
   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',	/* 39 */
   '\'', '`',   0,		/* Left shift */
   '\\', 'z', 'x', 'c', 'v', 'b', 'n',			/* 49 */
   'm', ',', '.', '/',   0,				/* Right shift */
   '*',
   0,	/* Alt */
   ' ',	/* Space bar */
   0,	/* Caps lock */
   0,	/* 59 - F1 key ... > */
   0,   0,   0,   0,   0,   0,   0,   0,
   0,	/* < ... F10 */
   0,	/* 69 - Num lock*/
   0,	/* Scroll Lock */
   0,	/* Home key */
   0,	/* Up Arrow */
   0,	/* Page Up */
   '-',
   0,	/* Left Arrow */
   0,
   0,	/* Right Arrow */
   '+',
   0,	/* 79 - End key*/
   0,	/* Down Arrow */
   0,	/* Page Down */
   0,	/* Insert Key */
   0,	/* Delete Key */
   0,   0,   0,
   0,	/* F11 Key */
   0,	/* F12 Key */
   0,	/* All other keys are undefined */
};

void keyboard_handler(struct regs *r)
{
   unsigned char scancode;

   //if (inb(0x64) & 1 == 0) continue;   //check if scancode is ready

   /* Read from the keyboard's data buffer */
   scancode = inb(0x60);

   /* If the top bit of the byte we read from the keyboard is
   *  set, that means that a key has just been released */
   if (scancode & 0x80)
   {
      /* You can use this one to see if the user released the
      *  shift, alt, or control keys... */
   }
   else
   {
      /* Here, a key was just pressed. Please note that if you
      *  hold a key down, you will get repeated key press
      *  interrupts. */

      /* Just to show you how this works, we simply translate
      *  the keyboard scancode into an ASCII value, and then
      *  display it to the screen. You can get creative and
      *  use some flags to see if a shift is pressed and use a
      *  different layout, or you can add another 128 entries
      *  to the above layout to correspond to 'shift' being
      *  held. If shift is held using the larger lookup table,
      *  you would add 128 to the scancode when you look for it */

      term_write_char(kbdus[scancode]);

      //write_string("Scancode: ");
      //char buf[32];
      //itoa(scancode, buf, 10);
      //write_string(buf);
      //write_string("\n");
   }
}

void timer_phase(int hz)
{
   int divisor = 1193180 / hz;       /* Calculate our divisor */
   outb(0x43, 0x36);             /* Set our command byte 0x36 */
   outb(0x40, divisor & 0xFF);   /* Set low byte of divisor */
   outb(0x40, divisor >> 8);     /* Set high byte of divisor */
}


/* This will keep track of how many ticks that the system
*  has been running for */
volatile unsigned timer_ticks = 0;

/* Handles the timer. In this case, it's very simple: We
*  increment the 'timer_ticks' variable every time the
*  timer fires. By default, the timer fires 18.222 times
*  per second. Why 18.222Hz? Some engineer at IBM must've
*  been smoking something funky */

//volatile uint16_t *video = (volatile uint16_t *)0xB8000;

void timer_handler(struct regs *r)
{
   ++timer_ticks;

   //*video++ = make_vgaentry('x', COLOR_GREEN);
}

void init_PIT(void);

void kmain() {

   term_init();

   show_hello_message();

   idt_install();
   irq_install();

   //magic_debug_break();


   //init_PIT();
   timer_phase(1);

   irq_install_handler(0, timer_handler);
   irq_install_handler(1, keyboard_handler);

   IRQ_set_mask(0);

   timer_ticks = 0;
   sti();

   while (1) {

      unsigned val = timer_ticks;

      //if ((val % 10) == 0) {
         //char buf[32];
         //itoa(val, buf, 10);

         //write_string("ticks: ");
         //write_string(buf);
         //write_string("\n");

      //}
      halt();
   }
}
