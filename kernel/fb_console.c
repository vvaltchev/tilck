
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/fb_console.h>
#include <exos/term.h>
#include <exos/paging.h>
#include <exos/hal.h>
#include <exos/kmalloc.h>

#define fb_make_color(r, g, b) (((r) << 16) | ((g) << 8) | (b))


bool use_framebuffer;

static uptr fb_paddr;
static u32 fb_pitch;
static u32 fb_width;
static u32 fb_height;
static u8 fb_bpp;
static u32 fb_size;

static uptr fb_vaddr;
static u32 fb_term_rows;
static u32 fb_term_cols;

static const u32 color_black = fb_make_color(0, 0, 0);
static const u32 color_white = fb_make_color(255, 255, 255);

static bool cursor_enabled;
static int cursor_row;
static int cursor_col;
static u32 *under_cursor_buf;


/* video_interface */

void fb_set_char_at(char c, u8 color, int row, int col);
void fb_clear_row(int row_num);

void fb_scroll_up(u32 lines);
void fb_scroll_down(u32 lines);
bool fb_is_at_bottom(void);
void fb_scroll_to_bottom(void);
void fb_add_row_and_scroll(void);

void fb_move_cursor(int row, int col);
void fb_enable_cursor(void);
void fb_disable_cursor(void);

/* end video_interface */

static const video_interface framebuffer_vi =
{
   fb_set_char_at,
   fb_clear_row,
   fb_scroll_up,
   fb_scroll_down,
   fb_is_at_bottom,
   fb_scroll_to_bottom,
   fb_add_row_and_scroll,
   fb_move_cursor,
   fb_enable_cursor,
   fb_disable_cursor
};

void set_framebuffer_info_from_mbi(multiboot_info_t *mbi)
{
   use_framebuffer = true;

   fb_paddr = mbi->framebuffer_addr;
   fb_pitch = mbi->framebuffer_pitch;
   fb_width = mbi->framebuffer_width;
   fb_height = mbi->framebuffer_height;
   fb_bpp = mbi->framebuffer_bpp;
   fb_size = fb_pitch * fb_height;
}

void init_framebuffer_console(void)
{
   fb_vaddr = KERNEL_BASE_VA + (1024 - 64) * MB;

   map_pages(get_kernel_page_dir(),
             (void *)fb_vaddr,
             fb_paddr,
             (fb_size/PAGE_SIZE) + 1,
             false,
             true);

   psf2_header *h = (void *)&_binary_font_psf_start;

   fb_term_rows = fb_height / h->height;
   fb_term_cols = fb_width / h->width;

   under_cursor_buf = kmalloc(sizeof(u32) * h->width * h->height);
   VERIFY(under_cursor_buf != NULL);

   init_term(&framebuffer_vi, fb_term_rows, fb_term_cols, 0);
   printk("[fb_console] rows: %i, cols: %i\n", fb_term_rows, fb_term_cols);
}



static ALWAYS_INLINE void fb_draw_pixel(u32 x, u32 y, u32 color)
{
   ASSERT(x < fb_width);
   ASSERT(y < fb_height);

   // ASSUMPTION: bpp is assumed to be == 32.
   *(volatile u32 *)(fb_vaddr + (fb_pitch * y) + (x << 2)) = color;
}

static ALWAYS_INLINE u32 fb_read_pixel(u32 x, u32 y)
{
   ASSERT(x < fb_width);
   ASSERT(y < fb_height);

   return *(volatile u32 *)(fb_vaddr + (fb_pitch * y) + (x << 2));
}

void fb_draw_cursor(u32 x, u32 y, u32 color)
{
   psf2_header *h = (void *)&_binary_font_psf_start;
   for (u32 row = 0; row < h->height; row++)
      for (u32 col = 0; col < h->width; col++)
         fb_draw_pixel(x + col, y + row, color);
}

void fb_draw_char(u32 x, u32 y, u32 color, u32 c)
{
   psf2_header *h = (void *)&_binary_font_psf_start;
   ASSERT(c < h->glyphs_count);

   u8 *data = (u8 *)h + h->header_size + h->bytes_per_glyph * c;

   for (u32 row = 0; row < h->height; row++) {

      // ASSUMPTION: width is divisible by 8
      u8 d = *(data + row * (h->width >> 3));

      for (u32 bit = 0; bit < h->width; bit++)
         fb_draw_pixel(x + h->width - bit - 1,
                       y + row,
                       (d & (1 << bit)) ? color_white : color_black);
   }
}

void dump_psf2_header(void)
{
   psf2_header *h = (void *)&_binary_font_psf_start;
   printk("magic: %p\n", h->magic);

   if (h->magic != PSF2_FONT_MAGIC)
      panic("Magic != PSF2\n");

   printk("header size: %u%s\n",
          h->header_size,
          h->header_size > sizeof(psf2_header) ? " > sizeof(psf2_header)" : "");
   printk("flags: %p\n", h->flags);
   printk("glyphs count: %u\n", h->glyphs_count);
   printk("bytes per glyph: %u\n", h->bytes_per_glyph);
   printk("font size: %u x %u\n", h->width, h->height);

   if (h->width % 8) {
      panic("Only fonts with width divisible by 8 are supported");
   }
}

void fb_save_under_cursor_buf(void)
{
   psf2_header *h = (void *)&_binary_font_psf_start;

   u32 ix = cursor_col * h->width;
   u32 iy = cursor_row * h->height;

   for (u32 y = 0; y < h->height; y++)
      for (u32 x = 0; x < h->width; x++)
         under_cursor_buf[y * h->width + x] = fb_read_pixel(ix + x, iy + y);
}

void fb_restore_under_cursor_buf(void)
{
   psf2_header *h = (void *)&_binary_font_psf_start;

   u32 ix = cursor_col * h->width;
   u32 iy = cursor_row * h->height;

   for (u32 y = 0; y < h->height; y++)
      for (u32 x = 0; x < h->width; x++)
         fb_draw_pixel(ix + x, iy + y, under_cursor_buf[y * h->width + x]);
}

/* video_interface */

void fb_set_char_at(char c, u8 color, int row, int col)
{
   psf2_header *h = (void *)&_binary_font_psf_start;
   fb_draw_char(col * h->width, row * h->height, color_white, c);

   if (row == cursor_row && col == cursor_col)
      fb_save_under_cursor_buf();
}

void fb_clear_row(int row_num)
{
   // Stub impl
   for (u32 i = 0; i < fb_term_cols; i++)
      fb_set_char_at(' ', 0, row_num, i);
}

void fb_scroll_up(u32 lines)
{

}

void fb_scroll_down(u32 lines)
{

}

bool fb_is_at_bottom(void)
{
   return true;
}

void fb_scroll_to_bottom(void)
{

}

void fb_add_row_and_scroll(void)
{

}

void fb_move_cursor(int row, int col)
{
   psf2_header *h = (void *)&_binary_font_psf_start;

   fb_restore_under_cursor_buf();

   cursor_row = row;
   cursor_col = col;

   if (cursor_enabled) {
      fb_save_under_cursor_buf();
      fb_draw_cursor(cursor_col * h->width, cursor_row * h->height, color_white);
   }
}

void fb_enable_cursor(void)
{
   cursor_enabled = true;
   fb_move_cursor(cursor_row, cursor_col);
}

void fb_disable_cursor(void)
{
   cursor_enabled = false;
   fb_move_cursor(cursor_row, cursor_col);
}
