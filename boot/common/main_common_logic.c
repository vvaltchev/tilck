/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>
#include <tilck_gen_headers/config_kernel.h>

#if ARCH_BITS == 32
   #define USE_ELF32
#else
   #define USE_ELF64
#endif

#include <tilck/common/basic_defs.h>
#include <tilck/common/assert.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/color_defs.h>
#include <tilck/common/elf_calc_mem_size.c.h>
#include <tilck/common/elf_get_section.c.h>
#include <tilck/common/build_info.h>

#include "common_int.h"

#define CHECK(cond)                                  \
   do {                                              \
      if (!(cond)) {                                 \
         printk("CHECK '%s' FAILED\n", #cond);       \
         return false;                               \
      }                                              \
   } while(0)

static video_mode_t selected_mode = INVALID_VIDEO_MODE;
static char kernel_file_path[64] = KERNEL_FILE_PATH;
static char line_buf[64];
static void *kernel_elf_file_paddr;
static struct build_info *kernel_build_info;
static char *cmdline_buf;
static u32 cmdline_buf_sz;
static u8 mods_start[32];
static u8 mods_len[32];
static int mods_cnt;
static bool kmod_console;
static bool kmod_fb;
static bool kmod_serial;

void
write_bootloader_hello_msg(void)
{
   intf->set_color(COLOR_BRIGHT_WHITE);

   printk("----- Hello from Tilck's %s bootloader! -----\n\n",
          intf->efi ? "UEFI" : "legacy");

   intf->set_color(DEFAULT_FG_COLOR);
}

void *
load_kernel_image(void)
{
   return simple_elf_loader(kernel_elf_file_paddr);
}

void
write_ok_msg(void)
{
   intf->set_color(COLOR_GREEN);
   printk("[  OK  ]\n");
   intf->set_color(DEFAULT_FG_COLOR);
}

void
write_fail_msg(void)
{
   intf->set_color(COLOR_RED);
   printk("[ FAIL ]\n");
   intf->set_color(DEFAULT_FG_COLOR);
}

static bool
check_elf_kernel(void)
{
   Elf_Ehdr *header = kernel_elf_file_paddr;
   Elf_Phdr *phdr = (Elf_Phdr *)(header + 1);

   if (header->e_ident[EI_MAG0] != ELFMAG0 ||
       header->e_ident[EI_MAG1] != ELFMAG1 ||
       header->e_ident[EI_MAG2] != ELFMAG2 ||
       header->e_ident[EI_MAG3] != ELFMAG3)
   {
      printk("Not an ELF file\n");
      return false;
   }

   if (header->e_ehsize != sizeof(*header)) {
      printk("Not an ELF32 file\n");
      return false;
   }

   for (int i = 0; i < header->e_phnum; i++, phdr++) {

      // Ignore non-load segments.
      if (phdr->p_type != PT_LOAD)
         continue;

      CHECK(phdr->p_vaddr >= KERNEL_BASE_VA);
      CHECK(phdr->p_paddr >= KERNEL_PADDR);
   }

   return true;
}

size_t
get_loaded_kernel_mem_sz(void)
{
   if (!kernel_elf_file_paddr)
      panic("No loaded kernel");

   return elf_calc_mem_size(kernel_elf_file_paddr);
}

static void
parse_kernel_mods_list(void)
{
   char *mods = kernel_build_info->modules_list;
   char *begin = mods;
   u8 len;

   mods_cnt = 0;
   kmod_console = false;
   kmod_fb = false;
   kmod_serial = false;

   for (char *p = mods; *p; p++) {

      if (*p != ' ' && p[1])
         continue;

      len = p - begin + !p[1];

      if (!strncmp(begin, "console", 7))
         kmod_console = true;
      else if (!strncmp(begin, "fb", 2))
         kmod_fb = true;
      else if (!strncmp(begin, "serial", 6))
         kmod_serial = true;

      if (mods_cnt == ARRAY_SIZE(mods_start))
         continue;

      mods_start[mods_cnt] = begin - mods;
      mods_len[mods_cnt] = len;
      begin = p + 1;
      mods_cnt++;
   }
}

static bool
load_kernel_file(void)
{
   Elf_Shdr *section;
   Elf_Ehdr *header;

   printk("Loading the ELF kernel... ");

   if (!intf->load_kernel_file(kernel_file_path, &kernel_elf_file_paddr)) {
      write_fail_msg();
      return false;
   }

   if (!check_elf_kernel()) {
      write_fail_msg();
      return false;
   }

   header = kernel_elf_file_paddr;
   section = elf_get_section(header, ".tilck_info");

   if (!section) {
      printk("Not a Tilck ELF kernel file\n");
      write_fail_msg();
      return false;
   }

   kernel_build_info = (void *)((char *)header + section->sh_offset);
   parse_kernel_mods_list();

   if (kmod_console) {

      if (kmod_fb) {

         if (selected_mode == INVALID_VIDEO_MODE ||
             selected_mode == intf->text_mode)
         {
            selected_mode = g_defmode;
         }

      } else {
         selected_mode = intf->text_mode;
      }

   } else {
      selected_mode = INVALID_VIDEO_MODE;
   }

   write_ok_msg();
   return true;
}

static void
read_kernel_file_path(void)
{
   bool failed = false;

   while (true) {

      printk("Kernel file path: ");
      read_line(line_buf, sizeof(line_buf));

      if (!line_buf[0] && !failed) {
         printk("Keeping the current kernel file.\n");
         break;
      }

      if (line_buf[0] != '/') {
         printk("Invalid file path. Expected an absolute path.\n");
         continue;
      }

      strcpy(kernel_file_path, line_buf);

      if (!load_kernel_file()) {
         failed = true;
         continue;
      }

      break;
   }
}

static void
clear_screen(void)
{
   intf->clear_screen();
   write_bootloader_hello_msg();
}

static void
print_kernel_mods(void)
{
   static char prefix[]         = "    modules:  ";
   static char prefix_padding[] = "              ";

   char *mods = kernel_build_info->modules_list;
   int per_line_len = sizeof(prefix) - 1;
   char tmp[32];

   printk("%s", prefix);

   for (int i = 0; i < mods_cnt; i++) {

      strncpy(tmp, mods + mods_start[i], mods_len[i]);
      tmp[mods_len[i]] = 0;

      if (per_line_len + mods_len[i] > 75) {
         printk("\n%s", prefix_padding);
         per_line_len = sizeof(prefix) - 1;
      }

      printk("%s ", tmp);
      per_line_len += mods_len[i] + 1;
   }

   printk("\n");
}

static void
show_menu_item(const char *cmd,
               const char *name,
               const char *value,
               bool q,
               bool nl)
{
   const size_t namelen = strlen(name);
   char pad[] = "           ";

   intf->set_color(COLOR_GREEN);
   printk("%s", cmd);
   intf->set_color(DEFAULT_FG_COLOR);

   if (value) {

      if (namelen <= sizeof(pad) - 1)
         pad[sizeof(pad) - 1 - namelen] = 0;
      else
         pad[0] = 0;

      printk(") %s:%s ", name, pad);

      if (q) {
         intf->set_color(COLOR_MAGENTA);
         printk("\'");
      } else {
         intf->set_color(COLOR_CYAN);
      }

      printk("%s", value);

      if (q) {
         printk("\'");
      }

      intf->set_color(DEFAULT_FG_COLOR);

      if (nl)
         printk("\n");

   } else {

      printk(") %s\n", name);
   }
}

static bool
run_interactive_logic(void)
{
   struct generic_video_mode_info gi;
   struct commit_hash_and_date comm;
   bool wait_for_key;
   char buf[8];

   while (true) {

      wait_for_key = true;

      if (selected_mode != INVALID_VIDEO_MODE) {
         if (!intf->get_mode_info(selected_mode, &gi)) {
            printk("ERROR: get_mode_info() failed");
            return false;
         }
      }

      extract_commit_hash_and_date(kernel_build_info, &comm);

      intf->set_color(COLOR_GREEN);
      printk("Menu\n");
      intf->set_color(DEFAULT_FG_COLOR);
      printk("---------------------------------------------------\n");

      show_menu_item("k", "Kernel file", kernel_file_path, false, true);

      printk("    version:  %s\n", kernel_build_info->ver);
      printk("    commit:   %s", comm.hash);

      if (comm.dirty)
         printk(" (dirty)");
      else if (comm.tags[0])
         printk(" (%s)", comm.tags);

      printk("\n");
      printk("    date:     %s\n", comm.date);
      print_kernel_mods();
      printk("\n");

      show_menu_item("v", "Video mode", "", false, false);

      if (selected_mode != INVALID_VIDEO_MODE)
         show_mode(-1, &gi, false);
      else
         printk("<none>\n");

      show_menu_item("e", "Cmdline", cmdline_buf, true, true);
      show_menu_item("b", "Boot", NULL, false, true);
      printk("\n> ");

      buf[0] = 0;
      read_line(buf, sizeof(buf));

      switch (buf[0]) {

         case 0:
         case 'b':
            return true;

         case 'k':
            read_kernel_file_path();
            break;

         case 'v':

            if (selected_mode != INVALID_VIDEO_MODE && kmod_fb) {

               show_video_modes();
               printk("\n");
               selected_mode = get_user_video_mode_choice();
               wait_for_key = false;

            } else {
               printk("Cannot change video mode with this kernel\n");
            }

            break;

         case 'e':
            printk("Cmdline: ");
            read_line(cmdline_buf, MIN(70, (int)cmdline_buf_sz));
            wait_for_key = false;
            break;

         default:
            printk("Invalid command\n");
      }

      if (wait_for_key) {
         printk("Press ANY key to continue");
         intf->read_key();
      }

      clear_screen();
   }

   return true;
}

static void
wait_for_any_key(void)
{
   printk("Press ANY key to continue");
   intf->read_key();
}

bool
common_bootloader_logic(void)
{
   bool in_retry = false;

   fetch_all_video_modes_once();
   selected_mode = g_defmode;
   cmdline_buf = intf->get_cmdline_buf(&cmdline_buf_sz);

   if (!cmdline_buf)
      panic("No cmdline buffer");

   if (cmdline_buf_sz < CMDLINE_BUF_SZ)
      panic("Cmdline buffer too small");

   strcpy(cmdline_buf, "tilck "); /* 1st argument, always ignored */
   cmdline_buf += 6;
   cmdline_buf_sz -= 6;

   if (!load_kernel_file())
      return false;

   printk("\n");

retry:

   if (in_retry) {
      wait_for_any_key();
      clear_screen();
   }

   in_retry = true;

   if (BOOT_INTERACTIVE) {
      if (!run_interactive_logic())
         return false;
   }

   clear_screen();

   if (!intf->load_initrd()) {

      if (BOOT_INTERACTIVE)
         goto retry;

      return false;
   }

   if (selected_mode != INVALID_VIDEO_MODE) {

      if (!intf->set_curr_video_mode(selected_mode)) {

         if (BOOT_INTERACTIVE) {
            printk("ERROR: cannot set the selected video mode\n");
            goto retry;
         }

         /* In this other case, the current video mode will be kept */
      }

   } else {

      /* No video mode */

      if (kmod_serial) {

         intf->clear_screen();
         printk("<No video console>");

      } else {

         printk("\n");
         printk("*** FATAL ERROR ***\n");
         printk("The kernel supports neither video nor serial console.\n");

         if (kmod_console) {
            printk("The kernel supports text mode but text mode is NOT ");
            printk("available when\nbooting with UEFI.\n\n");
         }

         printk("Refuse to boot.\n");
         goto retry;
      }
   }

   return true;
}
