
#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>
#include <exos/common/color_defs.h>

#include <exos/kernel/term.h>
#include <exos/kernel/process.h>
#include <exos/kernel/interrupts.h>

#define PRINTK_COLOR COLOR_GREEN
#define PRINTK_RINGBUF_FLUSH_COLOR COLOR_CYAN
#define PRINTK_NOSPACE_IN_RBUF_FLUSH_COLOR COLOR_MAGENTA
#define PRINTK_PANIC_COLOR COLOR_GREEN

static bool
write_in_buf_str(char **buf_ref, char *buf_end, const char *s)
{
   char *ptr = *buf_ref;

   while (*s && ptr < buf_end) {
      *ptr++ = *s++;
   }

   *buf_ref = ptr;
   return ptr < buf_end;
}

static inline bool
write_in_buf_char(char **buf_ref, char *buf_end, char c)
{
   char *ptr = *buf_ref;
   *ptr++ = c;
   *buf_ref = ptr;
   return ptr < buf_end;
}

typedef struct {

   int left_padding;
   int right_padding;
   char *buf;
   char *buf_end;
   bool zero_lpad;

} snprintk_ctx;

static void
snprintk_ctx_reset_per_argument_state(snprintk_ctx *ctx)
{
   ctx->left_padding = 0;
   ctx->right_padding = 0;
   ctx->zero_lpad = false;
}

#define WRITE_CHAR(c)                                         \
   do {                                                       \
      if (!write_in_buf_char(&ctx->buf, ctx->buf_end, (c)))   \
         goto out;                                            \
   } while (0)

#define WRITE_STR(s) if (!write_str(ctx, s)) goto out;

static bool
write_str(snprintk_ctx *ctx, const char *str)
{
   int sl = strlen(str);
   int lpad = MAX(0, ctx->left_padding - sl);
   int rpad = MAX(0, ctx->right_padding - sl);

   ASSERT(!lpad || !rpad);

   for (int i = 0; i < lpad; i++)
      WRITE_CHAR(ctx->zero_lpad ? '0' : ' ');

   if (!write_in_buf_str(&ctx->buf, ctx->buf_end, (str)))
      goto out;

   for (int i = 0; i < rpad; i++)
      WRITE_CHAR(' ');

   return true;

out:
   return false;
}


int vsnprintk(char *initial_buf, size_t size, const char *fmt, va_list args)
{
   char intbuf[64];

   snprintk_ctx __ctx = {
      .left_padding = 0,
      .right_padding = 0,
      .zero_lpad = false,
      .buf = initial_buf,
      .buf_end = initial_buf + size
   };

   /* ctx has to be a pointer because of macros shared with WRITE_STR */
   snprintk_ctx *ctx = &__ctx;

   while (*fmt) {

      if (*fmt != '%') {
         WRITE_CHAR(*fmt++);
         continue;
      }

      // *fmt is '%'
      ++fmt;

      if (*fmt == '%')
         continue;

switch_case:

      switch (*fmt) {

      case '0':
         ctx->zero_lpad = true;
         fmt++;

         if (!*fmt)
            goto out; /* nothing after the %0 sequence */

         /* parse now the command letter by re-entering in the switch case */
         goto switch_case;

      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':

         ctx->left_padding = exos_strtol(fmt, &fmt, NULL);

         if (!*fmt)
            goto out; /* nothing after the %<number> sequence */

         /* parse now the command letter by re-entering in the switch case */
         goto switch_case;

      case '-':
         ctx->right_padding = exos_strtol(fmt + 1, &fmt, NULL);

         if (!*fmt)
            goto out; /* nothing after the %-<number> sequence */

         /* parse now the command letter by re-entering in the switch case */
         goto switch_case;

      case 'l':

         if (!*++fmt)
            goto out;

         if (*fmt == 'l') {

            if (!*++fmt)
               goto out;

            if (*fmt == 'u') {
               uitoa64_dec(va_arg(args, u64), intbuf);
               WRITE_STR(intbuf);
            } else if (*fmt == 'i' || *fmt == 'd') {
               itoa64(va_arg(args, s64), intbuf);
               WRITE_STR(intbuf);
            } else if (*fmt == 'x') {
               uitoa64_hex_fixed(va_arg(args, u64), intbuf);
               WRITE_STR(intbuf);
            }

         }
         break;

      case 'd':
      case 'i':
         itoa32(va_arg(args, s32), intbuf);
         WRITE_STR(intbuf);
         break;

      case 'u':
         uitoa32_dec(va_arg(args, u32), intbuf);
         WRITE_STR(intbuf);
         break;

      case 'x':
         uitoa32_hex(va_arg(args, u32), intbuf);
         WRITE_STR(intbuf);
         break;

      case 'c':
         WRITE_CHAR(va_arg(args, s32));

         if (ctx->right_padding > 0) {
            ctx->right_padding--;
            WRITE_STR(""); // write the padding
         }

         break;

      case 's':
         WRITE_STR(va_arg(args, const char *));
         break;

      case 'p':
         uitoa32_hex_fixed(va_arg(args, uptr), intbuf);
         WRITE_STR("0x");
         WRITE_STR(intbuf);
         break;

      default:
         WRITE_CHAR('%');
         WRITE_CHAR(*fmt);
      }

      snprintk_ctx_reset_per_argument_state(ctx);
      ++fmt;
   }

out:
   ctx->buf[ ctx->buf < ctx->buf_end ? 0 : -1 ] = 0;
   return (ctx->buf - initial_buf);
}

int snprintk(char *buf, size_t size, const char *fmt, ...)
{
   int written;

   va_list args;
   va_start(args, fmt);
   written = vsnprintk(buf, size, fmt, args);
   va_end(args);

   return written;
}

typedef struct {

   union {

      struct {
         u32 used : 10;
         u32 read_pos : 10;
         u32 write_pos : 10;
         u32 in_printk : 1;
         u32 unused : 1;
      };

      u32 raw;
   };

} ringbuf_stat;

static char printk_rbuf[1024];
static volatile ringbuf_stat printk_rbuf_stat;

/*
 * NOTE: the ring buf cannot be larger than 1024 elems because the fields
 * 'used', 'read_pos' and 'write_pos' and 10 bits long and we CANNOT extend
 * them in 32 bits. Such approach is convenient because with everything packed
 * in 32 bits, we can do atomic operations.
 */
STATIC_ASSERT(sizeof(printk_rbuf) <= 1024);

static void printk_direct_flush(const char *buf, size_t size, u8 color)
{
   if (LIKELY(term_get_filter_func() != NULL)) {
      /* tty has been initialized and set a term write filter func */
      term_write(buf, size, color);
      return;
   }

   /*
    * tty has not been initialized yet, therefore we have to translate here
    * \n to \r\n, by writing character by character to term.
    */

   for (u32 i = 0; i < size; i++) {

      if (buf[i] == '\n') {
         term_write("\r", 1, color);
      }

      term_write(&buf[i], 1, color);
   }
}

void printk_flush_ringbuf(void)
{
   ringbuf_stat cs, ns;

   char minibuf[80];
   u32 to_read = 0;

   while (true) {

      do {
         cs = printk_rbuf_stat;
         ns = printk_rbuf_stat;

         /* We at most 'sizeof(minibuf)' bytes at a time */
         to_read = MIN(sizeof(minibuf), ns.used);

         /* And copy them to our minibuf */
         for (u32 i = 0; i < to_read; i++)
            minibuf[i] = printk_rbuf[(cs.read_pos + i) % sizeof(printk_rbuf)];

         /* Increase read_pos and decrease used */
         ns.read_pos = (ns.read_pos + to_read) % sizeof(printk_rbuf);
         ns.used -= to_read;

         if (!to_read)
            ns.in_printk = 0;

         /* Repeat that until we were able to do that atomically */

      } while (!BOOL_COMPARE_AND_SWAP(&printk_rbuf_stat.raw, cs.raw, ns.raw));

      /* Note: we check that in_printk in cs (current state) is unset! */
      if (!to_read)
         break;

      printk_direct_flush(minibuf, to_read, PRINTK_RINGBUF_FLUSH_COLOR);
   }
}

static void printk_append_to_ringbuf(const char *buf, size_t size)
{
   static const char err_msg[] = "{_DROPPED_}\n";

   ringbuf_stat cs, ns;

   do {
      cs = printk_rbuf_stat;
      ns = printk_rbuf_stat;

      if (cs.used + size >= sizeof(printk_rbuf)) {

         if (term_is_initialized()) {
            printk_direct_flush(buf, size, PRINTK_NOSPACE_IN_RBUF_FLUSH_COLOR);
            return;
         }

         if (buf != err_msg && cs.used < sizeof(printk_rbuf) - 1) {
            size = MIN(sizeof(printk_rbuf) - cs.used - 1, sizeof(err_msg));
            printk_append_to_ringbuf(err_msg, size);
         }

         return;
      }

      ns.used += size;
      ns.write_pos = (ns.write_pos + size) % sizeof(printk_rbuf);

   } while (!BOOL_COMPARE_AND_SWAP(&printk_rbuf_stat.raw, cs.raw, ns.raw));

   // Now we have some allocated space in the ringbuf

   for (u32 i = 0; i < size; i++)
      printk_rbuf[(cs.write_pos + i) % sizeof(printk_rbuf)] = buf[i];
}

void vprintk(const char *fmt, va_list args)
{
   static const char truncated_str[] = "[...]";

   char buf[256];
   int written = 0;
   bool prefix = in_panic() ? false : true;

   if (*fmt == PRINTK_CTRL_CHAR) {
      u32 cmd = *(u32 *)fmt;
      fmt += 4;

      if (cmd == *(u32 *)NO_PREFIX)
         prefix = false;
   }

   if (prefix)
      written = snprintk(buf, sizeof(buf), "[kernel] ");

   written += vsnprintk(buf + written, sizeof(buf) - written, fmt, args);

   if (written == sizeof(buf)) {

      /*
       * Corner case: the buffer is completely full and the final \0 has been
       * included in 'written'.
       */

      memcpy(buf + sizeof(buf) - sizeof(truncated_str),
             truncated_str,
             sizeof(truncated_str));

      written--;
   }

   if (!term_is_initialized()) {
      printk_append_to_ringbuf(buf, written);
      return;
   }

   if (in_panic()) {
      printk_direct_flush(buf, written, PRINTK_PANIC_COLOR);
      return;
   }

   disable_preemption();
   {
      ringbuf_stat cs, ns;

      do {
         cs = printk_rbuf_stat;
         ns = printk_rbuf_stat;
         ns.in_printk = 1;
      } while (!BOOL_COMPARE_AND_SWAP(&printk_rbuf_stat.raw, cs.raw, ns.raw));

      if (!cs.in_printk) {

         printk_direct_flush(buf, written, PRINTK_COLOR);
         printk_flush_ringbuf();

      } else {

         /* in_printk was already 1 */
         printk_append_to_ringbuf(buf, written);
      }

   }
   enable_preemption();
}

void printk(const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   vprintk(fmt, args);
   va_end(args);
}
