
#include <common_defs.h>
#include <string_util.h>
#include <term.h>
#include <fs/fat32.h>

const char *kernel_path = "/EFI/BOOT/kernel.bin";

void main(void)
{
   /* Necessary in order to PANIC to be able to show something on the screen. */
   term_init();

   fat_header *hdr = (fat_header *) RAM_DISK_PADDR;
   fat_entry *e = fat_search_entry(hdr, fat_unknown, kernel_path);

   if (e == NULL) {
      panic("Kernel file '%s' not found.", kernel_path);
   }

   fat_read_whole_file(hdr, e, (char*)KERNEL_PADDR, e->DIR_FileSize);

   /* Jump to the kernel */
   ((void (*)(void))KERNEL_PADDR)();
}
