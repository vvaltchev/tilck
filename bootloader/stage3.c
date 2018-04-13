
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/fat32_base.h>
#include <common/utils.h>
#include <elf.h>
#include "basic_term.h"

/*
 * Checks if 'addr' is in the range [begin, end).
 */
#define IN(addr, begin, end) ((begin) <= (addr) && (addr) < (end))

/*
 * Without forcing the CHS parameters, on QEMU the 40 MB image has the following
 * weird parameters:
 *
 * Cyclinders count:   49407
 * Heads per cylinder: 3
 * Sectors per track:  18
 *
 * Considering that: 49407*3*18*512 = ~1.27 GB, there must be something WRONG.
 *
 * And we get a CRC32 failure at 26M + 8K.
 *
 * On REAL HARDWARE, we get no checksum failures whatsoever.
 */

void ramdisk_checksum(void)
{
   u32 result = crc32(0, (void*)RAMDISK_PADDR, RAMDISK_SIZE);
   printk("RAMDISK CRC32: %p\n", result);

   // printk("Calculating the RAMDISK's CRC32...\n");
   // for (int k=0; k <= 16; k++) {
   //    u32 result = crc32(0, (void*)RAMDISK_PADDR, 26*MB + k*KB);
   //    printk("CRC32 for M=26, K=%u: %p\n", k, result);
   // }
}

void load_elf_kernel(const char *filepath, void **entry)
{
   fat_header *hdr = (fat_header *)RAMDISK_PADDR;
   void *free_space = (void *) (RAMDISK_PADDR + RAMDISK_SIZE);

   fat_entry *e = fat_search_entry(hdr, fat_get_type(hdr), filepath);

   if (!e)
      panic("Unable to open '%s'!\n", filepath);

   fat_read_whole_file(hdr, e, free_space, KERNEL_MAX_SIZE);

   Elf32_Ehdr *header = (Elf32_Ehdr *)free_space;

   VERIFY(header->e_ident[EI_MAG0] == ELFMAG0);
   VERIFY(header->e_ident[EI_MAG1] == ELFMAG1);
   VERIFY(header->e_ident[EI_MAG2] == ELFMAG2);
   VERIFY(header->e_ident[EI_MAG3] == ELFMAG3);
   VERIFY(header->e_ehsize == sizeof(*header));

   *entry = (void *)header->e_entry;

   Elf32_Phdr *phdrs = (Elf32_Phdr *)((char *)header + header->e_phoff);

   for (int i = 0; i < header->e_phnum; i++) {

      Elf32_Phdr *phdr = phdrs + i;

      if (phdr->p_type != PT_LOAD) {
         continue; // Ignore non-load segments.
      }

      VERIFY(phdr->p_vaddr >= KERNEL_BASE_VA);
      VERIFY(phdr->p_paddr >= KERNEL_PADDR);

      bzero((void *)phdr->p_paddr, phdr->p_memsz);

      memmove((void *)phdr->p_paddr,
              (char *)header + phdr->p_offset, phdr->p_filesz);

      if (IN(header->e_entry, phdr->p_vaddr, phdr->p_vaddr + phdr->p_filesz)) {
         /*
          * If e_entry is a vaddr (address >= KERNEL_BASE_VA), we need to
          * calculate its paddr because here paging is OFF. Therefore,
          * compute its offset from the beginning of the segment and add it
          * to the paddr of the segment.
          */
         *entry = (void *) (phdr->p_paddr + (header->e_entry - phdr->p_vaddr));
      }
   }
}

void bootloader_main(void)
{
   void *entry;

   /* Clear the screen in case we need to show a panic message */
   init_bt();

   /* Load the actual kernel ELF file */
   load_elf_kernel(KERNEL_FILE_PATH, &entry);

   /* Jump to the kernel */
   asmVolatile("jmpl *%0" : : "r"(entry));
}
