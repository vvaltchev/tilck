
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

static bool graphics_mode = false; // false = text mode
static u32 fb_paddr;
static u32 fb_pitch;
static u32 fb_width;
static u32 fb_height;
static u32 fb_bpp;

static u32 selected_mode = VGA_COLOR_TEXT_MODE_80x25; /* default */

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

bool is_resolution_known(u16 xres, u16 yres)
{
   if (xres == 640 && yres == 480)
      return true;

   if (xres == 800 && yres == 600)
      return true;

   if (xres == 1024 && yres == 768)
      return true;

   if (xres == 1280 && yres == 1024)
      return true;

   return false;
}

void ask_user_video_mode(void)
{
   VbeInfoBlock *vb = (void *)0x2000;
   ModeInfoBlock *mi = (void *)0x3000;

   u32 known_modes[10];
   int known_modes_count = 0;

   known_modes[known_modes_count++] = VGA_COLOR_TEXT_MODE_80x25;
   printk("Mode [0]: text mode 80 x 25 [DEFAULT]\n");

   vbe_get_info_block(vb);

   // printk("Query video modes\n");
   // printk("Vbe version: 0x%x\n", vb->VbeVersion);
   // printk("Vbe sig: %c%c%c%c\n", vb->VbeSignature[0],
   //        vb->VbeSignature[1], vb->VbeSignature[2], vb->VbeSignature[3]);
   // printk("EOM string: '%s'\n", get_flat_ptr(vb->OemStringPtr));

   u16 *modes = get_flat_ptr(vb->VideoModePtr);

   for (u32 i = 0; modes[i] != 0xffff; i++) {

      if (!vbe_get_mode_info(modes[i], mi))
         continue;

      /* skip text modes */
      if (!(mi->ModeAttributes & VBE_MODE_ATTRS_GFX_MODE))
         continue;

      /* skip graphics mode not supporting a linear framebuffer */
      if (!(mi->ModeAttributes & VBE_MODE_ATTRS_LINEAR_FB))
         continue;

      if (!(mi->ModeAttributes & VBE_MODE_ATTRS_SUPPORTED))
         continue;

      if (mi->MemoryModel != VB_MEM_MODEL_DIRECT_COLOR)
         continue;

      if (mi->BitsPerPixel < 24)
         continue;

      /* skip any evenutal fancy resolutions not known by exOS */
      if (!is_resolution_known(mi->XResolution, mi->YResolution))
         continue;

      printk("Mode [%d]: %d x %d x %d\n",
             known_modes_count, mi->XResolution, mi->YResolution,
             mi->BitsPerPixel);

      known_modes[known_modes_count++] = modes[i];
   }

   printk("\n");

   while (true) {

      printk("Select a video mode [%d - %d]: ", 0, known_modes_count - 1);

      char sel = bios_read_char();
      int s = sel - '0';

      if (sel == '\r') {
         selected_mode = VGA_COLOR_TEXT_MODE_80x25;
         printk("DEFAULT\n");
         break;
      }

      if (s < 0 || s > known_modes_count - 1) {
         printk("Invalid selection.\n");
         continue;
      }

      printk("%d\n\n", s);
      selected_mode = known_modes[s];
      break;
   }

   if (selected_mode == VGA_COLOR_TEXT_MODE_80x25) {
      graphics_mode = false;
      return;
   }

   if (!vbe_get_mode_info(selected_mode, mi))
      panic("vbe_get_mode_info(0x%x) failed", selected_mode);

   graphics_mode = true;
   fb_paddr = mi->PhysBasePtr;
   fb_width = mi->XResolution;
   fb_height = mi->YResolution;
   fb_pitch = mi->BytesPerScanLine;
   fb_bpp = mi->BitsPerPixel;

   // printk("Detailed mode info:\n");
   // printk("fb_paddr: %p\n", fb_paddr);
   // printk("fb_width: %u\n", fb_width);
   // printk("fb_height: %u\n", fb_height);
   // printk("fb_pitch: %u\n", fb_pitch);
   // printk("fb_bpp: %u\n", fb_bpp);
   // printk("LinBytesPerScanLine: %u\n", mi->LinBytesPerScanLine);
   // printk("MemoryModel: 0x%x\n", mi->MemoryModel);

   // printk("[ red ] mask size: %u, pos: %u\n", mi->RedMaskSize, mi->RedFieldPosition);
   // printk("[green] mask size: %u, pos: %u\n", mi->GreenMaskSize, mi->GreenFieldPosition);
   // printk("[blue ] mask size: %u, pos: %u\n", mi->BlueMaskSize, mi->BlueFieldPosition);

   // printk("press any key to continue\n");
   // bios_read_char();
}

void bootloader_main(void)
{
   void *entry;
   multiboot_info_t *mbi;

   vga_set_video_mode(VGA_COLOR_TEXT_MODE_80x25);
   init_bt();

   printk("----- Hello from exOS's legacy bootloader! -----\n\n");

   test_rm_call_working();

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
