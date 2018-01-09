
#include <common_defs.h>
#include <string_util.h>
#include <term.h>

#define RAM_DISK_PADDR (0x8000000U) // +128 M


/* ----- Declarations of asm functions ------ */

void jump_to_kernel(void);

/* ------------------------------------------- */


void main()
{
   term_init();
   printk("Hello from the 3rd stage of the bootloader!\n");

   while(1);
   //jump_to_kernel();
}
