/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/fat32_base.h>
#include <tilck/common/utils.h>
#include <tilck/common/arch/generic_x86/x86_utils.h>
#include <tilck/common/arch/generic_x86/cpu_features.h>

#include <elf.h>
#include <multiboot.h>

#include "basic_term.h"
#include "realmode_call.h"
#include "vbe.h"

#define RAMDISK_PADDR   (KERNEL_PADDR + KERNEL_MAX_SIZE)
#define MBI_PADDR       (0x10000)

/*
 * Checks if 'addr' is in the range [begin, end).
 */
#define IN(addr, begin, end) ((begin) <= (addr) && (addr) < (end))


typedef struct {

   u64 base;
   u64 len;
   u32 type;
   u32 acpi;

} mem_area_t;

#define BIOS_INT15h_READ_MEMORY_MAP        0xE820
#define BIOS_INT15h_READ_MEMORY_MAP_MAGIC  0x534D4150

#define MEM_USABLE             1
#define MEM_RESERVED           2
#define MEM_ACPI_RECLAIMABLE   3
#define MEM_ACPI_NVS_MEMORY    4
#define MEM_BAD                5

static inline u32 bios_to_multiboot_mem_region(u32 bios_mem_type)
{
   STATIC_ASSERT(MEM_USABLE == MULTIBOOT_MEMORY_AVAILABLE);
   STATIC_ASSERT(MEM_RESERVED == MULTIBOOT_MEMORY_RESERVED);
   STATIC_ASSERT(MEM_ACPI_RECLAIMABLE == MULTIBOOT_MEMORY_ACPI_RECLAIMABLE);
   STATIC_ASSERT(MEM_ACPI_NVS_MEMORY == MULTIBOOT_MEMORY_NVS);
   STATIC_ASSERT(MEM_BAD == MULTIBOOT_MEMORY_BADRAM);

   return bios_mem_type;
}


static mem_area_t *mem_areas = (void *)(16 * KB + sizeof(mem_area_t));
static u32 mem_areas_count = 0;

bool graphics_mode; // false = text mode

u32 fb_paddr;
u32 fb_pitch;
u32 fb_width;
u32 fb_height;
u32 fb_bpp;

u8 fb_red_pos;
u8 fb_red_mask_size;
u8 fb_green_pos;
u8 fb_green_mask_size;
u8 fb_blue_pos;
u8 fb_blue_mask_size;

u16 selected_mode = VGA_COLOR_TEXT_MODE_80x25; /* default */

u8 current_device;
u32 sectors_per_track;
u32 heads_per_cylinder;
u32 cylinders_count;

static u32 ramdisk_max_size;
static u32 ramdisk_used_bytes;
static u32 ramdisk_first_data_sector;

void ask_user_video_mode(void);

static void calculate_ramdisk_fat_size(void)
{
   fat_header *hdr = (fat_header *)RAMDISK_PADDR;
   const u32 sector_size = fat_get_sector_size(hdr);

   ramdisk_first_data_sector = fat_get_first_data_sector(hdr);
   ramdisk_max_size = fat_get_TotSec(hdr) * sector_size;
}

static void load_elf_kernel(const char *filepath, void **entry)
{
   fat_header *hdr = (fat_header *)RAMDISK_PADDR;
   void *free_space = (void *) (RAMDISK_PADDR + ramdisk_used_bytes);

   fat_entry *e = fat_search_entry(hdr, fat_get_type(hdr), filepath, NULL);

   if (!e) {
      panic("Unable to open '%s'!\n", filepath);
   }

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

      if (phdr->p_type != PT_LOAD)
         continue; // Ignore non-load segments.

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

static multiboot_info_t *setup_multiboot_info(void)
{
   multiboot_info_t *mbi;
   multiboot_module_t *mod;

   mbi = (multiboot_info_t *) MBI_PADDR;
   bzero(mbi, sizeof(*mbi));

   mod = (multiboot_module_t *)(MBI_PADDR + sizeof(*mbi));
   bzero(mod, sizeof(*mod));

   mbi->flags |= MULTIBOOT_INFO_MEMORY;
   mbi->mem_lower = 0;
   mbi->mem_upper = 0;

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
      mbi->framebuffer_bpp = (u8)fb_bpp;
      mbi->framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_RGB;
      mbi->framebuffer_red_field_position = fb_red_pos;
      mbi->framebuffer_red_mask_size = fb_red_mask_size;
      mbi->framebuffer_green_field_position = fb_green_pos;
      mbi->framebuffer_green_mask_size = fb_green_mask_size;
      mbi->framebuffer_blue_field_position = fb_blue_pos;
      mbi->framebuffer_blue_mask_size = fb_blue_mask_size;
   }

   mbi->flags |= MULTIBOOT_INFO_MODS;
   mbi->mods_addr = (u32)mod;
   mbi->mods_count = 1;
   mod->mod_start = RAMDISK_PADDR;

   /*
    * Pass via multiboot 'used bytes' as RAMDISK size instead of the real
    * RAMDISK size. This is useful if the kernel uses the RAMDISK read-only.
    */
   mod->mod_end = mod->mod_start + ramdisk_used_bytes;

   mbi->flags |= MULTIBOOT_INFO_MEM_MAP;

   multiboot_memory_map_t *mmmap =
      (void *)mbi->mods_addr + (mbi->mods_count * sizeof(multiboot_module_t));

   mbi->mmap_addr = (u32)mmmap;
   mbi->mmap_length = mem_areas_count * sizeof(multiboot_memory_map_t);

   for (u32 i = 0; i < mem_areas_count; i++) {

      mem_area_t *ma = mem_areas + i;

      if (ma->type == MEM_USABLE) {
         if (ma->base < mbi->mem_lower * KB)
            mbi->mem_lower = (u32)(ma->base / KB);

         if (ma->base + ma->len > mbi->mem_upper * KB)
            mbi->mem_upper = (u32)((ma->base + ma->len) / KB);
      }

      mmmap[i] = (multiboot_memory_map_t) {
         .size = sizeof(multiboot_memory_map_t) - sizeof(u32),
         .addr = ma->base,
         .len = ma->len,
         .type = bios_to_multiboot_mem_region(ma->type),
      };
   }

   return mbi;
}

static void read_memory_map(void)
{
   typedef struct PACKED {

      u32 base_low;
      u32 base_hi;
      u32 len_low;
      u32 len_hi;
      u32 type;
      u32 acpi;

   } bios_mem_area_t;

   STATIC_ASSERT(sizeof(bios_mem_area_t) <= sizeof(mem_area_t));

   u32 eax, ebx, ecx, edx, esi, edi, flags;

   bios_mem_area_t *bios_mem_area = ((void *) (mem_areas - 1));
   bzero(bios_mem_area, sizeof(bios_mem_area_t));

   /* es = 0 */
   edi = (u32)bios_mem_area;
   ebx = 0;

   while (true) {

      mem_areas->acpi = 1;
      eax = BIOS_INT15h_READ_MEMORY_MAP;
      edx = BIOS_INT15h_READ_MEMORY_MAP_MAGIC;
      ecx = sizeof(bios_mem_area_t);

      realmode_call(&realmode_int_15h, &eax, &ebx,
                    &ecx, &edx, &esi, &edi, &flags);

      if (!ebx)
         break;

      if (flags & EFLAGS_CF) {

         if (mem_areas_count > 0)
            break;

         panic("Error while reading memory map: CF set");
      }

      if (eax != BIOS_INT15h_READ_MEMORY_MAP_MAGIC)
         panic("Error while reading memory map: eax != magic");

      mem_area_t m = {
         .base = bios_mem_area->base_low | ((u64)bios_mem_area->base_hi << 32),
         .len = bios_mem_area->len_low | ((u64)bios_mem_area->len_hi << 32),
         .type = bios_mem_area->type,
         .acpi = bios_mem_area->acpi,
      };

      memcpy(mem_areas + mem_areas_count, &m, sizeof(mem_area_t));
      mem_areas_count++;
   }
}

static void dump_mem_map(void)
{
   for (u32 i = 0; i < mem_areas_count; i++) {
      mem_area_t *m = mem_areas + i;
      printk("mem area 0x%llx - 0x%llx (%u)\n", m->base, m->len, m->type);
   }
}

static void poison_usable_memory(void)
{
   for (u32 i = 0; i < mem_areas_count; i++) {

      mem_area_t *ma = mem_areas + i;

      if (ma->type == MEM_USABLE && ma->base >= MB) {

         /* Poison only memory regions above the first MB */

         memset32((void *)(uptr)ma->base,
                  KMALLOC_FREE_MEM_POISON_VAL,
                  (u32)ma->len / 4);
      }
   }
}

void bootloader_main(void)
{
   void *entry;
   multiboot_info_t *mbi;
   u32 ramdisk_used_sectors;

   vga_set_video_mode(VGA_COLOR_TEXT_MODE_80x25);
   init_bt();

   /* Sanity check: the variables in BSS should be zero-filled */
   ASSERT(!graphics_mode);
   ASSERT(!fb_paddr);

   printk("----- Hello from Tilck's legacy bootloader! -----\n\n");

   /* Sanity check: realmode_call should be able to return all reg values */
   test_rm_call_working();

   get_cpu_features();

   if (!x86_cpu_features.edx1.pse) {
      panic("Sorry, but your CPU is too old: no PSE (page size extension)");
   }

   read_memory_map();
   //dump_mem_map();

#if BOOTLOADER_POISON_MEMORY
   poison_usable_memory();
#endif

   bool success =
      read_drive_params(current_device,
                        &sectors_per_track,
                        &heads_per_cylinder,
                        &cylinders_count);

   if (!success)
      panic("read_write_params failed");

   printk("Loading ramdisk... ");

   // Read FAT's header
   read_sectors(RAMDISK_PADDR, 2048, 1 /* read just 1 sector */);

   calculate_ramdisk_fat_size();

   // Now read all the meta-data up to the first data sector.
   read_sectors(RAMDISK_PADDR, 2048, ramdisk_first_data_sector + 1);

   // Finally we're able to determine how big is the fatpart (pure data)
   ramdisk_used_bytes = fat_get_used_bytes((void *)RAMDISK_PADDR);

   ramdisk_used_sectors = (ramdisk_used_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
   read_sectors(RAMDISK_PADDR, 2048, ramdisk_used_sectors);

   printk("[ OK ]\n");
   printk("Loading the ELF kernel... ");

   load_elf_kernel(KERNEL_FILE_PATH, &entry);

   printk("[ OK ]\n\n");

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
