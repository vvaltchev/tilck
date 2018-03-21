
#include <common_defs.h>
#include <string_util.h>
#include <fs/fat32.h>
#include <elf.h>
#include <utils.h>

void term_init(void);

// Hack to make the linker happy (printk() in string_utils needs it).
u32 disable_preemption_count = 1;

/*
 * Checks if 'addr' is in the range [begin, end).
 */
#define IN(addr, begin, end) ((begin) <= (addr) && (addr) < (end))

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

void kfree2(void *ptr, size_t n)
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

   *entry = (void *)header.e_entry;

   const ssize_t total_phdrs_size = header.e_phnum * sizeof(Elf32_Phdr);
   Elf32_Phdr *phdr = kmalloc(total_phdrs_size);
   VERIFY(phdr != NULL);

   ret = elf_file->fops.fseek(elf_file, header.e_phoff, SEEK_SET);
   VERIFY(ret == (ssize_t)header.e_phoff);

   ret = elf_file->fops.fread(elf_file, (void *)phdr, total_phdrs_size);
   ASSERT(ret == total_phdrs_size);

   for (int i = 0; i < header.e_phnum; i++, phdr++) {

      if (phdr->p_type != PT_LOAD) {
         continue; // Ignore non-load segments.
      }

      VERIFY(phdr->p_vaddr >= KERNEL_BASE_VA);
      VERIFY(phdr->p_paddr >= KERNEL_PADDR);

      bzero((void *)phdr->p_paddr, phdr->p_memsz);
      ret = elf_file->fops.fseek(elf_file, phdr->p_offset, SEEK_SET);
      VERIFY(ret == (ssize_t)phdr->p_offset);

      ret = elf_file->fops.fread(elf_file,(void*)phdr->p_paddr,phdr->p_filesz);
      VERIFY(ret == (ssize_t)phdr->p_filesz);

      if (IN(header.e_entry, phdr->p_vaddr, phdr->p_vaddr + phdr->p_filesz)) {
         /*
          * If e_entry is a vaddr (address >= KERNEL_BASE_VA), we need to
          * calculate its paddr because here paging is OFF. Therefore,
          * compute its offset from the beginning of the segment and add it
          * to the paddr of the segment.
          */
         *entry = (void *) (phdr->p_paddr + (header.e_entry - phdr->p_vaddr));
      }
   }

   root_fs->fclose(elf_file);
   kfree2(phdr, sizeof(*phdr));
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

void bootloader_main(void)
{
   term_init();

   ASSERT(heap_used == 0);  // Be sure that BSS has been zero-ed.
   ASSERT(root_fs == NULL); // As above.

   printk("*** HELLO from the 3rd stage of the BOOTLOADER ***\n");

   //ramdisk_checksum();
   //while(1) asmVolatile("hlt");

   root_fs = fat_mount_ramdisk((void *)RAMDISK_PADDR);

   void *entry;
   load_elf_kernel(kernel_path, &entry);

   /* Jump to the kernel */
   asmVolatile("jmpl *%0" : : "r"(entry));
}
