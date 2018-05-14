
#include <common/basic_defs.h>
#include <multiboot.h>

extern bool use_framebuffer;

#define PSF2_FONT_MAGIC 0x864ab572

typedef struct {
    u32 magic;
    u32 version;          /* zero */
    u32 header_size;
    u32 flags;            /* 0 if there's no unicode table */
    u32 glyphs_count;
    u32 bytes_per_glyph;
    u32 height;          /* height in pixels */
    u32 width;           /* width in pixels */
} psf2_header;

extern char _binary_font_psf_start;

void set_framebuffer_info_from_mbi(multiboot_info_t *mbi);
void init_framebuffer_console(void);
