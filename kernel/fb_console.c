
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/fb_console.h>
#include <exos/term.h>
#include <exos/paging.h>
#include <exos/hal.h>

bool use_framebuffer;

static uptr fb_addr;
static u32 fb_pitch;
static u32 fb_width;
static u32 fb_height;
static u8 fb_bpp;
static u32 fb_size;

static u32 fb_term_rows;
static u32 fb_term_cols;

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

   fb_addr = mbi->framebuffer_addr;
   fb_pitch = mbi->framebuffer_pitch;
   fb_width = mbi->framebuffer_width;
   fb_height = mbi->framebuffer_height;
   fb_bpp = mbi->framebuffer_bpp;
   fb_size = fb_pitch * fb_height;
}

void init_framebuffer_console(void)
{
   map_pages(get_kernel_page_dir(),
             (void *)fb_addr,
             fb_addr,
             (fb_size/PAGE_SIZE) + 1,
             false,
             true);

   psf2_header *h = (void *)&_binary_font_psf_start;

   fb_term_rows = fb_height / h->height;
   fb_term_cols = fb_width / h->width;

   init_term(&framebuffer_vi, fb_term_rows, fb_term_cols, 0);
   printk("[fb_console] rows: %i, cols: %i\n", fb_term_rows, fb_term_cols);
}

static ALWAYS_INLINE u32 fb_make_color(int r, int g, int b)
{
   return (r << 16) | (g << 8) | b;
}

static ALWAYS_INLINE void fb_draw_pixel(int x, int y, u32 color)
{
   // ASSUMPTION: bpp is assumed to be == 32.
   *(volatile u32 *)(fb_addr + (fb_pitch * y) + (x << 2)) = color;
}

void fb_draw_char(u32 x, u32 y, u32 color, u32 c)
{
   u32 black = fb_make_color(0, 0, 0);
   psf2_header *h = (void *)&_binary_font_psf_start;
   ASSERT(c < h->glyphs_count);

   u8 *data = (u8 *)h + h->header_size + h->bytes_per_glyph * c;

   for (u32 row = 0; row < h->height; row++) {

      // ASSUMPTION: width is divisible by 8
      u8 d = *(data + row * (h->width >> 3));

      for (u32 bit = 0; bit < h->width; bit++)
         fb_draw_pixel(x + (h->width - bit),
                       y + row,
                       d & (1 << bit) ? color : black);
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

/* video_interface */

void fb_set_char_at(char c, u8 color, int row, int col)
{
   psf2_header *h = (void *)&_binary_font_psf_start;
   u32 red_val = fb_make_color(255, 0, 0);

   fb_draw_char(col * h->width, row * h->height, red_val, c);
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

}

void fb_enable_cursor(void)
{

}

void fb_disable_cursor(void)
{

}
