
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/fat32_base.h>
#include <common/utils.h>

#include <elf.h>
#include <multiboot.h>

#include "basic_term.h"

#define RAMDISK_PADDR              (KERNEL_PADDR + KERNEL_MAX_SIZE)
#define MBI_PADDR (0x10000)

/*
 * Checks if 'addr' is in the range [begin, end).
 */
#define IN(addr, begin, end) ((begin) <= (addr) && (addr) < (end))


static u32 ramdisk_size;

void calculate_ramdisk_size(void)
{
   fat_header *fat_hdr = (fat_header *)RAMDISK_PADDR;
   u32 sector_size = fat_get_sector_size(fat_hdr);
   ramdisk_size = fat_get_TotSec(fat_hdr) * sector_size;
}

void load_elf_kernel(const char *filepath, void **entry)
{
   fat_header *hdr = (fat_header *)RAMDISK_PADDR;
   void *free_space = (void *) (RAMDISK_PADDR + ramdisk_size);

   /* DEBUG: poison the free memory, up to 128 MB */
   memset(free_space, 0xFA, (128 * MB - RAMDISK_PADDR - ramdisk_size));

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

multiboot_info_t *setup_multiboot_info(void)
{
   multiboot_info_t *mbi;
   multiboot_module_t *mod;

   mbi = (multiboot_info_t *) MBI_PADDR;
   bzero(mbi, sizeof(*mbi));

   mod = (multiboot_module_t *)(MBI_PADDR + sizeof(*mbi));
   bzero(mod, sizeof(*mod));

   mbi->mem_lower = 0;
   mbi->mem_upper = 127*1024; /* temp hack */

   mbi->flags |= MULTIBOOT_INFO_FRAMEBUFFER_INFO;
   mbi->framebuffer_addr = 0xB8000;
   mbi->framebuffer_pitch = 80 * 2;
   mbi->framebuffer_width = 80;
   mbi->framebuffer_height = 25;
   mbi->framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT;

   mbi->flags |= MULTIBOOT_INFO_MODS;
   mbi->mods_addr = (u32)mod;
   mbi->mods_count = 1;
   mod->mod_start = RAMDISK_PADDR;
   mod->mod_end = mod->mod_start + ramdisk_size;

   return mbi;
}


void realmode_func_set_video_mode();
void realmode_write_char();
void realmode_test_out();

void call_realmode_func_asm(void *func,
                            u32 *eax_ref,
                            u32 *ebx_ref,
                            u32 *ecx_ref,
                            u32 *edx_ref,
                            u32 *esi_ref,
                            u32 *edi_ref);

void call_realmode(void *func,
                   u32 *eax_ref,
                   u32 *ebx_ref,
                   u32 *ecx_ref,
                   u32 *edx_ref,
                   u32 *esi_ref,
                   u32 *edi_ref)
{
   ASSERT(func != NULL);
   ASSERT(eax_ref != NULL);
   ASSERT(ebx_ref != NULL);
   ASSERT(ecx_ref != NULL);
   ASSERT(edx_ref != NULL);
   ASSERT(esi_ref != NULL);
   ASSERT(edi_ref != NULL);

   call_realmode_func_asm(func, eax_ref, ebx_ref,
                          ecx_ref, edx_ref, esi_ref, edi_ref);
}

void call_realmode_by_val(void *func,
                          u32 eax,
                          u32 ebx,
                          u32 ecx,
                          u32 edx,
                          u32 esi,
                          u32 edi)
{
   ASSERT(func != NULL);
   call_realmode_func_asm(func, &eax, &ebx, &ecx, &edx, &esi, &edi);
}


void check_rm_out_regs(void)
{
   u32 eax, ebx, ecx, edx, esi, edi;

   call_realmode(realmode_test_out, &eax, &ebx, &ecx, &edx, &esi, &edi);

   printk("eax: %d\n", eax);
   printk("ebx: %d\n", ebx);
   printk("ecx: %d\n", ecx);
   printk("edx: %d\n", edx);
   printk("esi: %d\n", esi);
   printk("edi: %d\n", edi);
}

void bootloader_main(void)
{
   void *entry;
   multiboot_info_t *mbi;

   /* Clear the screen in case we need to show a panic message */
   init_bt();

   printk("before switch to real mode\n\n\n\n\n\n\n\n\n");

   check_rm_out_regs();

   const char *str = "Hi from C\r\n";
   //call_realmode_by_val(realmode_func_set_video_mode, 0, 0, 0, 0, 0, 0);

   while (*str) {
      call_realmode_by_val(realmode_write_char, (u32)*str, 0, 0, 0, 0, 0);
      str++;
   }

   printk("after switch to real mode\n");

   //asmVolatile("cli");
   //asmVolatile("hlt");

   calculate_ramdisk_size();

   /* Load the actual kernel ELF file */
   load_elf_kernel(KERNEL_FILE_PATH, &entry);

   mbi = setup_multiboot_info();

   /* Jump to the kernel */
   asmVolatile("jmp *%%ecx"
               : /* no output */
               : "a" (MULTIBOOT_BOOTLOADER_MAGIC),
                 "b" (mbi),
                 "c" (entry)
               : /* no clobber */);
}
