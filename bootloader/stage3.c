
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/fat32_base.h>
#include <common/utils.h>
#include <common/arch/generic_x86/x86_utils.h>

#include <elf.h>
#include <multiboot.h>

#include "basic_term.h"
#include "realmode_call.h"
#include "vbe.h"

#define RAMDISK_PADDR              (KERNEL_PADDR + KERNEL_MAX_SIZE)
#define MBI_PADDR (0x10000)

/*
 * Checks if 'addr' is in the range [begin, end).
 */
#define IN(addr, begin, end) ((begin) <= (addr) && (addr) < (end))

bool graphics_mode; // false = text mode
u32 fb_paddr;
u32 fb_pitch;
u32 fb_width;
u32 fb_height;
u32 fb_bpp;

u32 selected_mode = VGA_COLOR_TEXT_MODE_80x25; /* default */

u16 current_device;
u32 sectors_per_track;
u32 heads_per_cylinder;
u32 cylinders_count;

static u32 ramdisk_size;
void ask_user_video_mode(void);

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

   if (!graphics_mode) {
      mbi->framebuffer_addr = 0xB8000;
      mbi->framebuffer_pitch = 80 * 2;
      mbi->framebuffer_width = 80;
      mbi->framebuffer_height = 25;
      mbi->framebuffer_bpp = 4;
      mbi->framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT;
   } else {
      mbi->framebuffer_addr = fb_paddr;
      mbi->framebuffer_pitch = fb_pitch;
      mbi->framebuffer_width = fb_width;
      mbi->framebuffer_height = fb_height;
      mbi->framebuffer_bpp = fb_bpp;
      mbi->framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_RGB;
   }

   mbi->flags |= MULTIBOOT_INFO_MODS;
   mbi->mods_addr = (u32)mod;
   mbi->mods_count = 1;
   mod->mod_start = RAMDISK_PADDR;
   mod->mod_end = mod->mod_start + ramdisk_size;

   return mbi;
}

uint32_t
crc32(uint32_t crc, const void *buf, size_t size);

void bootloader_main(void)
{
   void *entry;
   multiboot_info_t *mbi;

   vga_set_video_mode(VGA_COLOR_TEXT_MODE_80x25);
   init_bt();

   /* Sanity check: the variables in BSS should be zero-filled */
   ASSERT(!graphics_mode);
   ASSERT(!fb_paddr);
   ASSERT(!ramdisk_size);

   printk("----- Hello from exOS's legacy bootloader! -----\n\n");

   /* Sanity check: realmode_call should be able to return all reg values */
   test_rm_call_working();

   bool success =
      read_drive_params(current_device,
                        &sectors_per_track,
                        &heads_per_cylinder,
                        &cylinders_count);

   if (!success)
      panic("read_write_params failed");

   // printk("cylinders_count:    %d\n", cylinders_count);
   // printk("heads_per_cylinder: %d\n", heads_per_cylinder);
   // printk("sectors_per_track:  %d\n", sectors_per_track);
   // bios_read_char();


   printk("Loading ramdisk...\n");

   read_sectors(RAMDISK_PADDR, 2048, (4 * MB) / SECTOR_SIZE);
   printk("Ramdisk CRC32: %p\n", crc32(0, (const void *)RAMDISK_PADDR, (4 * MB)));

   calculate_ramdisk_size();

   /* Load the actual kernel ELF file */
   load_elf_kernel(KERNEL_FILE_PATH, &entry);

   ask_user_video_mode();

   while (!vbe_set_video_mode(selected_mode)) {
      printk("ERROR: unable to set the selected video mode!\n");
      printk("       vbe_set_video_mode(0x%x) failed.\n\n", selected_mode);
      printk("Please select a different video mode.\n\n");
      ask_user_video_mode();
   }

   mbi = setup_multiboot_info();

   /* Jump to the kernel */
   asmVolatile("jmp *%%ecx"
               : /* no output */
               : "a" (MULTIBOOT_BOOTLOADER_MAGIC),
                 "b" (mbi),
                 "c" (entry)
               : /* no clobber */);
}
