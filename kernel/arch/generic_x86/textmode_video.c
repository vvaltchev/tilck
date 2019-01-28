/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>
#include <tilck/common/color_defs.h>

#include <tilck/kernel/arch/generic_x86/textmode_video.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/term.h>

#define VIDEO_ADDR ((u16 *) KERNEL_PA_TO_VA(0xB8000))
#define VIDEO_COLS 80
#define VIDEO_ROWS 25

static void textmode_clear_row(u16 row_num, u8 color)
{
   ASSERT(row_num < VIDEO_ROWS);

   memset16(VIDEO_ADDR + VIDEO_COLS * row_num,
            make_vgaentry(' ', color),
            VIDEO_COLS);
}

static void textmode_set_char_at(int row, int col, u16 entry)
{
   ASSERT(0 <= row && row < VIDEO_ROWS);
   ASSERT(0 <= col && col < VIDEO_COLS);

   volatile u16 *video = (volatile u16 *)VIDEO_ADDR;
   video[row * VIDEO_COLS + col] = entry;
}

static void textmode_set_row(int row, u16 *data, bool flush)
{
   ASSERT(0 <= row && row < VIDEO_ROWS);

   void *dest_addr = VIDEO_ADDR + row * VIDEO_COLS;
   void *src_addr = data;

  /*
   * TODO: investigate why here SSE+ instructions cannot always be used,
   * even if we are in a FPU context, while the fb_console (fb_raw.c) can use
   * them without issues in (apparently) the same context.
   *
   * Details: while using QEMU + KVM, we get a "KVM internal error. Suberror: 1"
   * if any AVX mov instructions are used, while SSE 2 instructions work fine.
   * Without KVM, QEMU does not support AVX therefore we can use only up to SSE3
   * and no issues are experiences. On real hardware, in most of the cases, SSE
   * instructions work, while AVX don't. On Intel Celeron N3160 not even SSE
   * instruction can be used in this context. This is certainly an issue related
   * with this specific context, as in general both SSE+ and AVX+ instructions
   * can be used in Tilck, as fb_raw.c shows. In theory textmode_set_row() is
   * executed exactly in the same context as fb_set_row_optimized(), but in
   * practice there seems to be at least one difference.
   *
   * My current (random) guess: could the difference be that in this case we're
   * accessing the low-mem (< 1 MB) ? The fb_console accesses high IO-mapped
   * memory instead.
   *
   */
   memcpy32(dest_addr, src_addr, VIDEO_COLS >> 1);
}

/*
 * This function works, but in practice is 2x slower than just using term's
 * generic scroll and re-draw the whole screen.
 */
static void textmode_scroll_one_line_up(void)
{
   memcpy32(VIDEO_ADDR,
            VIDEO_ADDR + VIDEO_COLS,
            ((VIDEO_ROWS - 1) * VIDEO_COLS) >> 1);
}

/*
 * -------- cursor management functions -----------
 *
 * Here: http://www.osdever.net/FreeVGA/vga/textcur.htm
 * There is a lot of precious information about how to work with the cursor.
 */

static void textmode_move_cursor(u16 row, u16 col, int color /* ignored */)
{
   u16 position = (row * VIDEO_COLS) + col;

   // cursor LOW port to vga INDEX register
   outb(0x3D4, 0x0F);
   outb(0x3D5, (u8)(position & 0xFF));
   // cursor HIGH port to vga INDEX register
   outb(0x3D4, 0x0E);
   outb(0x3D5, (u8)((position >> 8) & 0xFF));
}

static void textmode_enable_cursor(void)
{
   const u8 scanline_start = 0;
   const u8 scanline_end = 15;

   outb(0x3D4, 0x0A);
   outb(0x3D5, (inb(0x3D5) & 0xC0) | scanline_start); // Note: mask with 0xC0
                                                      // which keeps only the
                                                      // higher 2 bits in order
                                                      // to set bit 5 to 0.

   outb(0x3D4, 0x0B);
   outb(0x3D5, (inb(0x3D5) & 0xE0) | scanline_end);   // Mask with 0xE0 keeps
                                                      // the higher 3 bits.
}

static void textmode_disable_cursor(void)
{
   /*
    * Move the cursor off-screen. Yes, it seems an ugly way to do that, but it
    * seems to be the most compatible way to "disable" the cursor.
    * As claimed here: http://www.osdever.net/FreeVGA/vga/textcur.htm#enable
    * the "official" method below (commented) does not work on some hardware.
    * On my Hannspree SN10E1, I can confirm that the code below causes strange
    * effects: the cursor is offset-ed 3 chars at the right of the position
    * it should be.
    */
   textmode_move_cursor(VIDEO_ROWS, VIDEO_COLS, 0);

   // outb(0x3D4, 0x0A);
   // outb(0x3D5, inb(0x3D5) | 0x20);
}

static const video_interface ega_text_mode_i =
{
   textmode_set_char_at,
   textmode_set_row,
   textmode_clear_row,
   textmode_move_cursor,
   textmode_enable_cursor,
   textmode_disable_cursor,
   NULL, /* textmode_scroll_one_line_up (see the comment) */
   NULL, /* flush_buffers */
   NULL, /* redraw_static_elements */
   NULL, /* disable_static_elems_refresh */
   NULL  /* enable_static_elems_refresh */
};

void init_textmode_console(void)
{
   page_directory_t *pdir = get_curr_pdir();

   if (pdir != NULL && !is_mapped(pdir, VIDEO_ADDR)) {
      int rc = map_page(pdir,
                        VIDEO_ADDR,
                        KERNEL_VA_TO_PA(VIDEO_ADDR),
                        false,
                        true);

      if (rc < 0)
         panic("textmode_console: unable to map VIDEO_ADDR in the virt space");
   }

   init_term(get_curr_term(), &ega_text_mode_i, VIDEO_ROWS, VIDEO_COLS);
}
