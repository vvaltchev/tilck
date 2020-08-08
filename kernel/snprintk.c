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

   va_list args;
   enum printk_width width;
   int left_padding;
   int right_padding;
   char *buf;
   char *buf_end;
   bool zero_lpad;
   bool hash_sign;
   char intbuf[64];
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

      if (fmtX != 'c')
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

static bool
write_char_param(struct snprintk_ctx *ctx, char fmtX)
{
   ctx->intbuf[0] = (char)va_arg(ctx->args, long);
   ctx->intbuf[1] = 0;
   return write_str(ctx, 'c', ctx->intbuf);
}

static bool
write_string_param(struct snprintk_ctx *ctx, char fmtX)
{
   return write_str(ctx, fmtX, va_arg(ctx->args, const char *));
}

static bool
write_pointer_param(struct snprintk_ctx *ctx, char fmtX)
{
   uitoaN_hex_fixed(va_arg(ctx->args, ulong), ctx->intbuf);
   return write_str(ctx, fmtX, ctx->intbuf);
}

static bool
write_number_param(struct snprintk_ctx *ctx, char fmtX)
{
   ulong width = width_val[ctx->width];
   u8 base = diuox_base[(u8)fmtX];
   char *intbuf = ctx->intbuf;
   ASSERT(base);

   if (fmtX == 'd' || fmtX == 'i') {

      if (ctx->width == pw_long_long)
         itoa64(va_arg(ctx->args, s64), intbuf);
      else
         itoaN(sign_extend(va_arg(ctx->args, long), width), intbuf);

   } else {

      if (ctx->width == pw_long_long)
         uitoa64(va_arg(ctx->args, u64), intbuf, base);
      else
         uitoaN(va_arg(ctx->args, ulong) & make_bitmask(width), intbuf, base);
   }

   return write_str(ctx, fmtX, intbuf);
}

typedef bool (*write_param_func)(struct snprintk_ctx *, char);

static const write_param_func write_funcs[32] =
{
   ['d' - 97] = &write_number_param,
   ['i' - 97] = &write_number_param,
   ['o' - 97] = &write_number_param,
   ['u' - 97] = &write_number_param,
   ['x' - 97] = &write_number_param,
   ['c' - 97] = &write_char_param,
   ['s' - 97] = &write_string_param,
   ['p' - 97] = &write_pointer_param,
};

int vsnprintk(char *initial_buf, size_t size, const char *fmt, va_list __args)
{
   static const enum printk_width double_mods[2][3] =
   {
      /* 'l' modifier */
      { pw_default, pw_long, pw_long_long },

      /* 'h' modifier */
      { pw_default, pw_short, pw_char },
   };

   static const enum printk_width single_mods[2] =
   {
      /* 'z' modifier */
      pw_long,

      /* 'L', 'q', 'j' modifiers */
      pw_long_long,
   };

   struct snprintk_ctx __ctx;

   /* ctx has to be a pointer because of macros */
   struct snprintk_ctx *ctx = &__ctx;
   snprintk_ctx_reset_state(ctx);
   ctx->buf = initial_buf;
   ctx->buf_end = initial_buf + size;
   va_copy(ctx->args, __args);

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

next_char_in_seq:

      if (isalpha_lower(*fmt)) {

         u8 idx = (u8)*fmt - 97;

         if (write_funcs[idx]) {
            if (!write_funcs[idx](ctx, *fmt))
               goto out;

            goto end_sequence;
         }
      }

      switch (*fmt) {

      case '0':
         ctx->zero_lpad = true;

         if (!*++fmt)
            goto truncated_seq;

         /* parse now the command letter by re-entering in the switch case */
         goto next_char_in_seq;

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
         goto next_char_in_seq;

      case '-':
         ctx->right_padding = (int)tilck_strtol(fmt + 1, &fmt, 10, NULL);

         if (!*fmt)
            goto truncated_seq;

         /* parse now the command letter by re-entering in the switch case */
         goto next_char_in_seq;

      case '#':

         if (ctx->hash_sign) {

            if (!*++fmt)
               goto incomplete_seq; /* note: forcing "%#" to be printed */

            goto next_char_in_seq; /* skip this '#' and move on */
         }

         if (fmt[-1] != '%')
            goto incomplete_seq;

         if (!fmt[1])
            goto unknown_seq;

         fmt++;
         ctx->hash_sign = true;
         goto next_char_in_seq;

      case 'z': /* fall-through */
      case 'j': /* fall-through */
      case 'q': /* fall-through */
      case 'L':

         if (ctx->width != pw_default)
            goto unknown_seq;

         ctx->width = single_mods[*fmt++ != 'z'];

         if (!*fmt)
            goto truncated_seq;

         goto next_char_in_seq;

      case 'l': /* fall-through */
      case 'h':

         {
            int idx = -1;
            int m = *fmt != 'l';    /* choose the right sub-array: 'l' or 'h' */

            /* Find the index in the sub-array with the current width mod */
            for (int i = 0; i < 3; i++) {
               if (ctx->width == double_mods[m][i])
                  idx = i;
            }

            /* Invalid current width modifier */
            if (idx < 0 || idx == 2)
               goto unknown_seq;             /* %lll, %hhh, %lh, %hl, ... */

            /* Check for string end */
            if (!*++fmt)
               goto truncated_seq;

            /* Move to the next modifier (e.g. default -> long -> long long) */
            ctx->width = double_mods[m][idx + 1];
            goto next_char_in_seq;
         }

      default:

unknown_seq:
incomplete_seq:

         WRITE_CHAR('%');

         if (ctx->hash_sign)
            WRITE_CHAR('#');

         WRITE_CHAR(*fmt);
         goto end_sequence;
      }

      /* Make `break` unusable in the switch, in order to avoid confusion */
      NOT_REACHED();

end_sequence:
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
