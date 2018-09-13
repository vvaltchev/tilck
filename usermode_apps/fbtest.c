#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdbool.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <time.h>

#define FB_DEVICE "/dev/fb0"
#define TTY_DEVICE "/dev/tty"

#define FB_ASSUMPTION(x)                                        \
   if (!(x)) {                                                  \
      fprintf(stderr, "fb mode assumption '%s' failed\n", #x);  \
      return false;                                             \
   }

struct fb_var_screeninfo fbi;
struct fb_fix_screeninfo fb_fixinfo;

char *buffer;
size_t fb_size;
size_t fb_pitch;
size_t fb_pitch_div4;
int fbfd = -1, ttyfd = -1;

/*
 * Set 'n' 32-bit elems pointed by 's' to 'val'.
 */
static inline void *memset32(void *s, uint32_t val, size_t n)
{
   unsigned unused; /* See the comment in strlen() about the unused variable */

   __asm__ volatile ("rep stosl"
               : "=D" (unused), "=a" (val), "=c" (n)
               :  "D" (s), "a" (val), "c" (n)
               : "cc", "memory");

   return s;
}

static inline uint32_t make_color(uint8_t red, uint8_t green, uint8_t blue)
{
   return red << fbi.red.offset |
          green << fbi.green.offset |
          blue << fbi.blue.offset;
}

static inline void set_pixel(uint32_t x, uint32_t y, uint32_t color)
{
   ((volatile uint32_t *)buffer)[x + y * fb_pitch_div4] = color;
}

void clear_screen(uint32_t color)
{
   memset32(buffer, color, fb_size >> 2);
}

void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
   for (uint32_t cy = y; cy < y + h; cy++)
      memset32(buffer + cy * fb_pitch + x, color, w);
}

static bool check_fb_assumptions(void)
{
   FB_ASSUMPTION(fbi.bits_per_pixel == 32);

   FB_ASSUMPTION((fbi.red.offset % 8) == 0);
   FB_ASSUMPTION((fbi.green.offset % 8) == 0);
   FB_ASSUMPTION((fbi.blue.offset % 8) == 0);
   FB_ASSUMPTION((fbi.transp.offset % 8) == 0);

   FB_ASSUMPTION(fbi.red.length == 8);
   FB_ASSUMPTION(fbi.green.length == 8);
   FB_ASSUMPTION(fbi.blue.length == 8);
   FB_ASSUMPTION(fbi.transp.length == 0);

   FB_ASSUMPTION(fbi.xoffset == 0);
   FB_ASSUMPTION(fbi.yoffset == 0);

   FB_ASSUMPTION(fbi.red.msb_right == 0);
   FB_ASSUMPTION(fbi.green.msb_right == 0);
   FB_ASSUMPTION(fbi.blue.msb_right == 0);

   return true;
}

bool fb_acquire(void)
{
   fbfd = open(FB_DEVICE, O_RDWR);

   if (fbfd < 0) {
      fprintf(stderr, "unable to open '%s'\n", FB_DEVICE);
      return false;
   }

   if (ioctl(fbfd, FBIOGET_FSCREENINFO, &fb_fixinfo) != 0) {
      fprintf(stderr, "ioctl(FBIOGET_FSCREENINFO) failed\n");
      return false;
   }

   if (ioctl (fbfd, FBIOGET_VSCREENINFO, &fbi) != 0) {
      fprintf(stderr, "ioctl(FBIOGET_VSCREENINFO) failed\n");
      return false;
   }

   fb_pitch = fb_fixinfo.line_length;
   fb_size = fb_pitch * fbi.yres;
   fb_pitch_div4 = fb_pitch >> 2;

   if (!check_fb_assumptions())
      return false;

   ttyfd = open(TTY_DEVICE, O_RDWR);

   if (ttyfd < 0) {
      fprintf(stderr, "Unable to open '%s'\n", TTY_DEVICE);
      return false;
   }

   if (ioctl(ttyfd, KDSETMODE, KD_GRAPHICS) != 0) {
      fprintf(stderr,
              "WARNING: unable set tty into graphics mode on '%s'\n", TTY_DEVICE);
   }

   buffer = mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);

   if (buffer == MAP_FAILED) {
      fprintf(stderr, "Unable to mmap framebuffer '%s'\n", FB_DEVICE);
      return false;
   }

   return true;
}

void fb_release(void)
{
   if (buffer)
      munmap(buffer, fb_size);

   if (ttyfd != -1) {
      ioctl(ttyfd, KDSETMODE, KD_TEXT);
      close(ttyfd);
   }

   if (fbfd != -1)
      close(fbfd);
}

void draw_something(void)
{
   clear_screen(make_color(0, 0, 0));
   draw_rect(100, 100, 300, 200, make_color(255, 0, 0));
}

void dump_fb_fix_info(void)
{
   fbfd = open(FB_DEVICE, O_RDWR);

   if (fbfd < 0) {
      fprintf(stderr, "unable to open '%s'\n", FB_DEVICE);
      return;
   }

   if (ioctl(fbfd, FBIOGET_FSCREENINFO, &fb_fixinfo) != 0) {
      fprintf(stderr, "ioctl(FBIOGET_FSCREENINFO) failed\n");
      return;
   }

   printf("id:          %s\n", fb_fixinfo.id);
   printf("smem_start:  %p\n", fb_fixinfo.smem_start);
   printf("smem_len:    %u\n", fb_fixinfo.smem_len);
   printf("type:        %u\n", fb_fixinfo.type);
   printf("visual:      %u\n", fb_fixinfo.visual);
   printf("xpanstep:    %u\n", fb_fixinfo.xpanstep);
   printf("ypanstep:    %u\n", fb_fixinfo.ypanstep);
   printf("ywrapstep:   %u\n", fb_fixinfo.ywrapstep);
   printf("line_length: %u\n", fb_fixinfo.line_length);
   printf("mmio_start:  %p\n", fb_fixinfo.mmio_start);
   printf("mmio_len:    %p\n", fb_fixinfo.mmio_len);
}

void dump_fb_var_info(void)
{
   fbfd = open(FB_DEVICE, O_RDWR);

   if (fbfd < 0) {
      fprintf(stderr, "unable to open '%s'\n", FB_DEVICE);
      return;
   }

   if (ioctl (fbfd, FBIOGET_VSCREENINFO, &fbi) != 0) {
      fprintf(stderr, "ioctl(FBIOGET_VSCREENINFO) failed\n");
      return;
   }

   printf("xres:           %u\n", fbi.xres);
   printf("yres:           %u\n", fbi.yres);
   printf("xres_virtual:   %u\n", fbi.xres_virtual);
   printf("yres_virtual:   %u\n", fbi.yres_virtual);
   printf("height (mm):    %u\n", fbi.height);
   printf("width (mm):     %u\n", fbi.width);
   printf("pixclock (ps):  %u\n", fbi.pixclock);

   printf("red:\n");
   printf("    offset:     %u\n", fbi.red.offset);
   printf("    length:     %u\n", fbi.red.length);
   printf("    msb_right:  %u\n", fbi.red.msb_right);

   printf("green:\n");
   printf("    offset:     %u\n", fbi.green.offset);
   printf("    length:     %u\n", fbi.green.length);
   printf("    msb_right:  %u\n", fbi.green.msb_right);

   printf("blue:\n");
   printf("    offset:     %u\n", fbi.blue.offset);
   printf("    length:     %u\n", fbi.blue.length);
   printf("    msb_right:  %u\n", fbi.blue.msb_right);
}

int main(int argc, char **argv)
{
   if (argc > 1) {

      if (!strcmp(argv[1], "-fi"))
         dump_fb_fix_info();
      else if (!strcmp(argv[1], "-vi"))
         dump_fb_var_info();
      else
         printf("Unknown option '%s'\n", argv[1]);

      return 0;
   }

   if (!fb_acquire()) {
      fb_release();
      return 1;
   }

   draw_something();
   getchar();

   fb_release();
   return 0;
}
