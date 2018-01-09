
#include <common_defs.h>
#include <string_util.h>
#include <term.h>
#include <fs/fat32.h>


/* ----- Declarations of asm functions ------ */

NORETURN void jump_to_kernel(void);

/* ------------------------------------------- */


NORETURN void main(void)
{
   term_init();
   printk("Hello from the 3rd stage of the bootloader!\n");

   fat_header *hdr = (fat_header *) RAM_DISK_PADDR;
   fat_entry *e = fat_search_entry(hdr, fat_unknown, "/EFI/BOOT/kernel.bin");

   fat_read_whole_file(hdr, e, (char*)KERNEL_PADDR, e->DIR_FileSize);

   jump_to_kernel();
}
