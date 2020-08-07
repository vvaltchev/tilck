/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

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

struct snprintk_ctx {

   int left_padding;
   int right_padding;
   char *buf;
   char *buf_end;
   bool zero_lpad;
   bool hash_sign;
};

static void
snprintk_ctx_reset_per_argument_state(struct snprintk_ctx *ctx)
{
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

      if (ctx->hash_sign && (fmtX == 'x' || fmtX == 'o')) {

         WRITE_CHAR('0');

         if (fmtX == 'x')
            WRITE_CHAR('x');
      }
   }

   for (int i = 0; i < lpad; i++)
      WRITE_CHAR(pad_char);

   if (ctx->hash_sign && pad_char != '0') {

      if (fmtX == 'x' || fmtX == 'o') {

         WRITE_CHAR('0');

         if (fmtX == 'x')
            WRITE_CHAR('x');
      }
   }

   if (!write_in_buf_str(&ctx->buf, ctx->buf_end, (str)))
      goto out;

   for (int i = 0; i < rpad; i++)
      WRITE_CHAR(pad_char);

   return true;

out:
   return false;
}

#define UOX_ITOA(base)                                   \
   if (fmt[-1] == '%')                                   \
      uitoa32(va_arg(args, u32), intbuf, base);          \
   else /* fmt[-1] is 'l' or 'z' */                      \
      uitoaN(va_arg(args, ulong), intbuf, base);

static u8 diuox_base[128] =
{
   ['d'] = 10,
   ['i'] = 10,
   ['u'] = 10,
   ['o'] = 8,
   ['x'] = 16,
};

int vsnprintk(char *initial_buf, size_t size, const char *fmt, va_list args)
{
   char intbuf[64];

   struct snprintk_ctx __ctx = {
      .left_padding = 0,
      .right_padding = 0,
      .zero_lpad = false,
      .buf = initial_buf,
      .buf_end = initial_buf + size,
   };

   /* ctx has to be a pointer because of macros shared with WRITE_STR */
   struct snprintk_ctx *ctx = &__ctx;

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

         goto switch_case;

      // %l makes the following type (d, i, o, u, x) a long.
      case 'l':

         if (!*++fmt)
            goto truncated_seq;

         // %ll make the following type a long long.
         if (*fmt == 'l') {

            if (!*++fmt)
               goto truncated_seq;

            if (!diuox_base[(u8)*fmt])
               goto incomplete_seq;

            if (*fmt == 'i' || *fmt == 'd')
               itoa64(va_arg(args, s64), intbuf);
            else
               uitoa64(va_arg(args, u64), intbuf, diuox_base[(u8)*fmt]);

            WRITE_STR(intbuf);
            break;
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
         WRITE_STR("0x");
         WRITE_STR(intbuf);
         break;

      default:

         if (diuox_base[(u8)*fmt]) {

            if (*fmt == 'd' || *fmt == 'i') {

               if (fmt[-1] == '%')
                  itoa32(va_arg(args, s32), intbuf);
               else /* 'l' or 'z' */
                  itoaN(va_arg(args, long), intbuf);

            } else {

               UOX_ITOA(diuox_base[(u8)*fmt]);
            }

            WRITE_STR(intbuf);

         } else {

incomplete_seq:

            WRITE_CHAR('%');

            if (ctx->hash_sign)
               WRITE_CHAR('#');

            WRITE_CHAR(*fmt);
         }
      }

      snprintk_ctx_reset_per_argument_state(ctx);
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
