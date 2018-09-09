#include <stdio.h>
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

#define fbdev "/dev/fb0"
#define ttydev "/dev/tty"

#define fb_mode_assumption(x)                                   \
   if (!(x)) {                                                  \
      fprintf(stderr, "fb mode assumption '%s' failed\n", #x);  \
      return false;                                             \
   }

struct fb_var_screeninfo fbinfo;
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
   return red << fbinfo.red.offset |
          green << fbinfo.green.offset |
          blue << fbinfo.blue.offset;
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
   fb_mode_assumption(fbinfo.bits_per_pixel == 32);

   fb_mode_assumption((fbinfo.red.offset % 8) == 0);
   fb_mode_assumption((fbinfo.green.offset % 8) == 0);
   fb_mode_assumption((fbinfo.blue.offset % 8) == 0);
   fb_mode_assumption((fbinfo.transp.offset % 8) == 0);

   fb_mode_assumption(fbinfo.red.length == 8);
   fb_mode_assumption(fbinfo.green.length == 8);
   fb_mode_assumption(fbinfo.blue.length == 8);
   fb_mode_assumption(fbinfo.transp.length == 0);

   fb_mode_assumption(fbinfo.xoffset == 0);
   fb_mode_assumption(fbinfo.yoffset == 0);

   fb_mode_assumption(fbinfo.red.msb_right == 0);
   fb_mode_assumption(fbinfo.green.msb_right == 0);
   fb_mode_assumption(fbinfo.blue.msb_right == 0);

   return true;
}

bool fb_acquire(void)
{
   fbfd = open(fbdev, O_RDWR);

   if (fbfd < 0) {
      fprintf(stderr, "unable to open '%s'\n", fbdev);
      return false;
   }

   if (ioctl(fbfd, FBIOGET_FSCREENINFO, &fb_fixinfo) != 0) {
      fprintf(stderr, "ioctl(FBIOGET_FSCREENINFO) failed\n");
      return false;
   }

   if (ioctl (fbfd, FBIOGET_VSCREENINFO, &fbinfo) != 0) {
      fprintf(stderr, "ioctl(FBIOGET_VSCREENINFO) failed\n");
      return false;
   }

   fb_pitch = fb_fixinfo.line_length;
   fb_size = fb_pitch * fbinfo.yres;
   fb_pitch_div4 = fb_pitch >> 2;

   if (!check_fb_assumptions())
      return false;

   ttyfd = open(ttydev, O_RDWR);

   if (ttyfd < 0) {
      fprintf(stderr, "Unable to open '%s'\n", ttydev);
      return false;
   }

   if (ioctl(ttyfd, KDSETMODE, KD_GRAPHICS) != 0) {
      fprintf(stderr, "Unable set tty into graphics mode on '%s'\n", ttydev);
      return false;
   }

   buffer = mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);

   if (buffer == MAP_FAILED) {
      fprintf(stderr, "Unable to mmap framebuffer '%s'\n", fbdev);
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

int main(int argc, char **argv)
{
   if (!fb_acquire()) {
      fb_release();
      return 1;
   }

   draw_something();
   getchar();

   fb_release();
   return 0;
}
