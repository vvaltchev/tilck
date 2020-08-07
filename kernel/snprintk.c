/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

/* Check for the 'j' length modifier (intmax_t) */
STATIC_ASSERT(sizeof(intmax_t) == sizeof(long long));

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

enum printk_width {
   pw_long_long = 0,
   pw_long      = 1,
   pw_default   = 2,
   pw_short     = 3,
   pw_char      = 4
};

static const ulong width_val[] =
{
   [pw_long_long] = 0, /* unused */
   [pw_long]      = 8 * sizeof(long),
   [pw_default]   = 8 * sizeof(int),
   [pw_short]     = 8 * sizeof(short),
   [pw_char]      = 8 * sizeof(char),
};

struct snprintk_ctx {

   enum printk_width width;
   int left_padding;
   int right_padding;
   char *buf;
   char *buf_end;
   bool zero_lpad;
   bool hash_sign;
};

static void
snprintk_ctx_reset_state(struct snprintk_ctx *ctx)
{
   ctx->width = pw_default;
   ctx->left_padding = 0;
   ctx->right_padding = 0;
   ctx->zero_lpad = false;
   ctx->hash_sign = false;
}

#define WRITE_CHAR(c)                                         \
   do {                                                       \
      if (!write_in_buf_char(&ctx->buf, ctx->buf_end, (c)))   \
         goto out;                                            \
   } while (0)

#define WRITE_STR(s) if (!write_str(ctx, *fmt, s)) goto out;

static bool
write_0x_prefix(struct snprintk_ctx *ctx, char fmtX)
{
   if (fmtX == 'x' || fmtX == 'p' || fmtX == 'o') {

      WRITE_CHAR('0');

      if (fmtX == 'x' || fmtX == 'p')
         WRITE_CHAR('x');
   }

   return true;

out:
   return false;
}

static bool
write_str(struct snprintk_ctx *ctx, char fmtX, const char *str)
{
   int sl = (int) strlen(str);
   int lpad = MAX(0, ctx->left_padding - sl);
   int rpad = MAX(0, ctx->right_padding - sl);
   char pad_char = ' ';

   /* Cannot have both left padding _and_ right padding */
   ASSERT(!lpad || !rpad);

   if (ctx->hash_sign) {

      int off = 0;

      if (fmtX == 'x')
         off = 2;
      else if (fmtX == 'o')
         off = 1;

      lpad = MAX(0, lpad - off);
      rpad = MAX(0, rpad - off);
   }

   if (ctx->zero_lpad) {

      pad_char = '0';

      if (ctx->hash_sign) {
         if (!write_0x_prefix(ctx, fmtX))
            goto out;
      }
   }

   for (int i = 0; i < lpad; i++)
      WRITE_CHAR(pad_char);

   if ((fmtX == 'p' || ctx->hash_sign) && pad_char != '0') {
      if (!write_0x_prefix(ctx, fmtX))
         goto out;
   }

   if (!write_in_buf_str(&ctx->buf, ctx->buf_end, (str)))
      goto out;

   for (int i = 0; i < rpad; i++)
      WRITE_CHAR(pad_char);

   return true;

out:
   return false;
}

static const u8 diuox_base[128] =
{
   ['d'] = 10,
   ['i'] = 10,
   ['u'] = 10,
   ['o'] = 8,
   ['x'] = 16,
};

int vsnprintk(char *initial_buf, size_t size, const char *fmt, va_list args)
{
   struct snprintk_ctx __ctx;
   char intbuf[64];
   ulong width;
   u8 base;

   /* ctx has to be a pointer because of macros shared with WRITE_STR */
   struct snprintk_ctx *ctx = &__ctx;
   snprintk_ctx_reset_state(ctx);
   ctx->buf = initial_buf;
   ctx->buf_end = initial_buf + size;

   while (*fmt) {

      // *fmt != '%', just write it and continue.
      if (*fmt != '%') {
         WRITE_CHAR(*fmt++);
         continue;
      }

      // *fmt is '%' ...
      ++fmt;

      // after the '%' ...

      if (*fmt == '%' || (u8)*fmt >= 128) {
         /* %% or % followed by non-ascii char */
         WRITE_CHAR(*fmt++);
         continue;
      }

      // after the '%', follows an ASCII char != '%' ...

switch_case:

      switch (*fmt) {

      case '0':
         ctx->zero_lpad = true;

         if (!*++fmt)
            goto truncated_seq;

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

         ctx->left_padding = (int)tilck_strtol(fmt, &fmt, 10, NULL);

         if (!*fmt)
            goto truncated_seq;

         /* parse now the command letter by re-entering in the switch case */
         goto switch_case;

      case '-':
         ctx->right_padding = (int)tilck_strtol(fmt + 1, &fmt, 10, NULL);

         if (!*fmt)
            goto truncated_seq;

         /* parse now the command letter by re-entering in the switch case */
         goto switch_case;

      case '#':

         if (fmt[-1] != '%')
            break; /* ignore misplaced '#' */

         if (!*++fmt)
            goto truncated_seq;

         ctx->hash_sign = true;
         goto switch_case;

      // %z (followed by d, i, o, u, x) is C99 prefix for size_t
      case 'z':

         if (!*++fmt)
            goto truncated_seq;

         ctx->width = pw_long;
         goto switch_case;

      case 'j': /* fall-through */
      case 'q': /* fall-through */
      case 'L':

         if (!*++fmt)
            goto truncated_seq;

         ctx->width = pw_long_long;
         goto switch_case;

      // %l makes the following type (d, i, o, u, x) a long.
      case 'l':

         if (ctx->width == pw_default) {

            if (!*++fmt)
               goto truncated_seq;

            ctx->width = pw_long;

         } else if (ctx->width == pw_long) {

            if (!*++fmt)
               goto truncated_seq;

            ctx->width = pw_long_long;

         } else {

            goto unknown_seq;             /* %lll */
         }

         goto switch_case;

      case 'h':

         if (ctx->width == pw_default) {

            if (!*++fmt)
               goto truncated_seq;

            ctx->width = pw_short;

         } else if (ctx->width == pw_short) {

            if (!*++fmt)
               goto truncated_seq;

            ctx->width = pw_char;

         } else {

            goto unknown_seq;             /* %hhh */
         }

         goto switch_case;

      case 'c':
         WRITE_CHAR((char) va_arg(args, s32));

         if (ctx->right_padding > 0) {
            ctx->right_padding--;
            WRITE_STR(""); // write the padding
         }

         break;

      case 's':
         WRITE_STR(va_arg(args, const char *));
         break;

      case 'p':
         uitoaN_hex_fixed(va_arg(args, ulong), intbuf);
         WRITE_STR(intbuf);
         break;

      default:

         base = diuox_base[(u8)*fmt];
         width = width_val[ctx->width];

         if (!base)
            goto unknown_seq;

         if (*fmt == 'd' || *fmt == 'i') {

            if (ctx->width == pw_long_long)
               itoa64(va_arg(args, s64), intbuf);
            else
               itoaN(sign_extend(va_arg(args, long), width), intbuf);

         } else {

            if (ctx->width == pw_long_long)
               uitoa64(va_arg(args, u64), intbuf, base);
            else
               uitoaN(va_arg(args, ulong) & make_bitmask(width), intbuf, base);
         }

         WRITE_STR(intbuf);
         break;


unknown_seq:
incomplete_seq:

         WRITE_CHAR('%');

         if (ctx->hash_sign)
            WRITE_CHAR('#');

         WRITE_CHAR(*fmt);
      }

      snprintk_ctx_reset_state(ctx);
      ++fmt;
   }

out:
truncated_seq:
   ctx->buf[ ctx->buf < ctx->buf_end ? 0 : -1 ] = 0;
   return (int)(ctx->buf - initial_buf);
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
