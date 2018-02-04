
#include <common_defs.h>
#include <string_util.h>
#include <term.h>
#include <fs/fat32.h>
#include <kmalloc.h>
#include <elf.h>
#include <utils.h>
#include <config.h>

#define VADDR_TO_PADDR(x) ((void *)( (uptr)(x) - KERNEL_BASE_VA ))

const char *kernel_path = "/EFI/BOOT/elf_kernel_stripped";

char small_heap[4096];
size_t heap_used;
filesystem *root_fs;

void *kmalloc(size_t n)
{
   if (heap_used + n >= sizeof(small_heap)) {
      panic("kmalloc: unable to allocate %u bytes!", n);
   }

   void *result = small_heap + heap_used;
   heap_used += n;

   return result;
}

void kfree(void *ptr, size_t n)
{
   /* DO NOTHING */
}


void load_elf_kernel(const char *filepath, void **entry)
{
   ssize_t ret;
   Elf32_Ehdr header;

   fat_file_handle *elf_file = root_fs->fopen(root_fs, filepath);

   if (!elf_file) {
      panic("Unable to open '%s'!\n", filepath);
   }

   ret = elf_file->fops.fread(elf_file, (void *)&header, sizeof(header));
   ASSERT(ret == sizeof(header));

   ASSERT(header.e_ident[EI_MAG0] == ELFMAG0);
   ASSERT(header.e_ident[EI_MAG1] == ELFMAG1);
   ASSERT(header.e_ident[EI_MAG2] == ELFMAG2);
   ASSERT(header.e_ident[EI_MAG3] == ELFMAG3);

   ASSERT(header.e_ehsize == sizeof(header));

   const ssize_t total_phdrs_size = header.e_phnum * sizeof(Elf32_Phdr);
   Elf32_Phdr *phdr = kmalloc(total_phdrs_size);
   VERIFY(phdr != NULL);

   ret = elf_file->fops.fread(elf_file, (void *)phdr, total_phdrs_size);
   ASSERT(ret == total_phdrs_size);

   for (int i = 0; i < header.e_phnum; i++, phdr++) {

      // Ignore non-load segments.
      if (phdr->p_type != PT_LOAD) {
         continue;
      }

      VERIFY(phdr->p_vaddr >= KERNEL_BASE_VA);

      bzero(VADDR_TO_PADDR(phdr->p_vaddr), phdr->p_memsz);

      ret = elf_file->fops.fseek(elf_file, phdr->p_offset, SEEK_SET);
      VERIFY(ret == (ssize_t)phdr->p_offset);

      ret = elf_file->fops.fread(elf_file, VADDR_TO_PADDR(phdr->p_vaddr), phdr->p_filesz);
      VERIFY(ret == (ssize_t)phdr->p_filesz);
   }

   root_fs->fclose(elf_file);

   *entry = (void *) header.e_entry;
   kfree(phdr, sizeof(*phdr));
}

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

void main(void)
{
   term_init();

   ASSERT(heap_used == 0);  // Be sure that BSS has been zero-ed.
   ASSERT(root_fs == NULL); // As above.

   printk("*** HELLO from the 3rd stage of the BOOTLOADER ***\n");

   // ramdisk_checksum();
   // while(1) halt();

   root_fs = fat_mount_ramdisk((void *)RAMDISK_PADDR);

   void *entry;
   load_elf_kernel(kernel_path, &entry);

   entry = VADDR_TO_PADDR(entry);

   /* Jump to the kernel */
   asmVolatile("jmpl *%0" : : "r"(entry));
}
