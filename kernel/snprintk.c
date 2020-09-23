/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

/* Check for the 'j' length modifier (intmax_t) */
STATIC_ASSERT(sizeof(intmax_t) == sizeof(long long));

static bool
write_in_buf_str(char **buf_ref, char *buf_end, const char *s, int len)
{
   char *ptr = *buf_ref;

   if (len >= 0) {

      while (*s && len > 0 && ptr < buf_end) {
         *ptr++ = *s++;
         len--;
      }

   } else {

      while (*s && ptr < buf_end) {
         *ptr++ = *s++;
      }
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

   /* Fundamental context variables */
   const char *fmt;
   va_list args;
   char *buf;        /* dest buffer */
   char *buf_end;    /* dest buffer's end */

   /* The following params are reset on each call of process_seq */
   enum printk_width width;
   int lpad;
   int rpad;
   int precision;
   bool zero_lpad;
   bool hash_sign;

   /* Helper buffers */
   char intbuf[64];
};

static void
snprintk_ctx_reset_state(struct snprintk_ctx *ctx)
{
   ctx->width = pw_default;
   ctx->lpad = 0;
   ctx->rpad = 0;
   ctx->precision = -1;
   ctx->zero_lpad = false;
   ctx->hash_sign = false;
}

#define WRITE_CHAR(c)                                         \
   do {                                                       \
      if (!write_in_buf_char(&ctx->buf, ctx->buf_end, (c)))   \
         goto out_of_dest_buffer;                             \
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

out_of_dest_buffer:
   return false;
}

static bool
write_str(struct snprintk_ctx *ctx, char fmtX, const char *str)
{
   int sl = (int) strlen(str);
   int lpad = MAX(0, ctx->lpad - sl);
   int rpad = MAX(0, ctx->rpad - sl);
   bool zero_lpad = ctx->zero_lpad;
   char pad_char = ' ';

   /* Cannot have both left padding _and_ right padding */
   ASSERT(!lpad || !rpad);

   if (ctx->precision >= 0 && fmtX != 's') {
      /* Note: we don't support floating point numbers */
      zero_lpad = true;
      lpad = MAX(0, ctx->precision - sl);
   }

   if (ctx->hash_sign) {

      int off = 0;

      if (fmtX == 'x')
         off = 2;
      else if (fmtX == 'o')
         off = 1;

      lpad = MAX(0, lpad - off);
      rpad = MAX(0, rpad - off);
   }

   if (zero_lpad) {

      if (fmtX != 'c')
         pad_char = '0';

      if (ctx->hash_sign) {
         if (!write_0x_prefix(ctx, fmtX))
            goto out_of_dest_buffer;
      }
   }

   for (int i = 0; i < lpad; i++)
      WRITE_CHAR(pad_char);

   if ((fmtX == 'p' || ctx->hash_sign) && pad_char != '0') {
      if (!write_0x_prefix(ctx, fmtX))
         goto out_of_dest_buffer;
   }

   if (!write_in_buf_str(&ctx->buf, ctx->buf_end, str, ctx->precision))
      goto out_of_dest_buffer;

   for (int i = 0; i < rpad; i++)
      WRITE_CHAR(pad_char);

   return true;

out_of_dest_buffer:
   return false;
}

static const u8 diuox_base[128] =
{
   ['d'] = 10,
   ['i'] = 10,
   ['u'] = 10,
   ['o'] = 8,
   ['x'] = 16,
   ['X'] = 16,
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
   const char *str = va_arg(ctx->args, const char *);

   if (!str)
      str = "(null)";

   return write_str(ctx, fmtX, str);
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

   if (fmtX == 'X') {

      fmtX = 'x';

      for (char *p = ctx->intbuf; *p; p++)
         *p = (char) toupper(*p);
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

static const enum printk_width double_mods[2][3] =
{
   /* 'l' modifier */
   { pw_default, pw_long, pw_long_long },

   /* 'h' modifier */
   { pw_default, pw_short, pw_char },
};

static const enum printk_width single_mods[2] =
{
   /* 'z' and 't' modifiers */
   pw_long,

   /* 'L', 'q', 'j' modifiers */
   pw_long_long,
};

static bool
process_seq(struct snprintk_ctx *ctx)
{
   /* Here're just after '%' */
   if (*ctx->fmt == '%' || (u8)*ctx->fmt >= 128) {
      /* %% or % followed by non-ascii char */
      WRITE_CHAR(*ctx->fmt);
      return true;
   }

   goto process_next_char_in_seq;

move_to_next_char_in_seq:
   ctx->fmt++;

process_next_char_in_seq:

   if (!*ctx->fmt)
      goto truncated_seq;

   if (isalpha_lower(*ctx->fmt)) {

      u8 idx = (u8)*ctx->fmt - 97;

      if (write_funcs[idx]) {
         if (!write_funcs[idx](ctx, *ctx->fmt))
            goto out_of_dest_buffer;

         goto end_sequence;
      }

   } else if (*ctx->fmt == 'X') {

      if (!write_number_param(ctx, 'X'))
         goto out_of_dest_buffer;

      goto end_sequence;
   }

   switch (*ctx->fmt) {

   case '0':
      ctx->zero_lpad = true;
      goto move_to_next_char_in_seq;

   case '1':
   case '2':
   case '3':
   case '4':
   case '5':
   case '6':
   case '7':
   case '8':
   case '9':
      ctx->lpad = (int)tilck_strtol(ctx->fmt, &ctx->fmt, 10, NULL);
      goto process_next_char_in_seq;

   case '*':
      /* Exactly like the 0-9 case, but we must take the value from a param */
      ctx->lpad = (int)va_arg(ctx->args, long);

      if (ctx->lpad < 0) {
         ctx->rpad = -ctx->lpad;
         ctx->lpad = 0;
      }

      goto move_to_next_char_in_seq;

   case '-':
      if (ctx->fmt[1] != '*') {
         ctx->rpad = (int)tilck_strtol(ctx->fmt + 1, &ctx->fmt, 10, NULL);
      } else {
         ctx->rpad = (int)va_arg(ctx->args, long);
         if (ctx->rpad < 0)
            ctx->rpad = -ctx->rpad;
         ctx->fmt += 2; /* skip '-' and '*' */
      }
      goto process_next_char_in_seq;

   case '.':

      if (ctx->fmt[1] != '*') {
         ctx->precision = (int)tilck_strtol(ctx->fmt + 1, &ctx->fmt, 10, NULL);
      } else {
         ctx->precision = MAX(0, (int)va_arg(ctx->args, long));
         ctx->fmt += 2;
      }
      goto process_next_char_in_seq;

   case '#':

      if (ctx->hash_sign) {

         if (!*++ctx->fmt)
            goto incomplete_seq; /* note: forcing "%#" to be printed */

         goto process_next_char_in_seq; /* skip this '#' and move on */
      }

      if (ctx->fmt[-1] != '%')
         goto incomplete_seq;

      if (!ctx->fmt[1])
         goto unknown_seq;

      ctx->hash_sign = true;
      goto move_to_next_char_in_seq;

   case 'z': /* fall-through */
   case 't': /* fall-through */
   case 'j': /* fall-through */
   case 'q': /* fall-through */
   case 'L':

      if (ctx->width != pw_default)
         goto unknown_seq;

      ctx->width = single_mods[*ctx->fmt != 'z' && *ctx->fmt != 't'];
      goto move_to_next_char_in_seq;

   case 'l': /* fall-through */
   case 'h':

      {
         int idx = -1;
         int m = *ctx->fmt != 'l';  /* choose the sub-array: 'l' or 'h' */

         /* Find the index in the sub-array with the current width mod */
         for (int i = 0; i < 3; i++) {
            if (ctx->width == double_mods[m][i])
               idx = i;
         }

         /* Invalid current width modifier */
         if (idx < 0 || idx == 2)
            goto unknown_seq;             /* %lll, %hhh, %lh, %hl, ... */

         /* Move to the next modifier (e.g. default -> long -> long long) */
         ctx->width = double_mods[m][idx + 1];
         goto move_to_next_char_in_seq;
      }

   default:

unknown_seq:
incomplete_seq:

      WRITE_CHAR('%');

      if (ctx->hash_sign)
         WRITE_CHAR('#');

      if (ctx->zero_lpad) {
         WRITE_CHAR('0');
         ctx->zero_lpad = false;
      }

      if (ctx->rpad) {
         ctx->lpad = -ctx->rpad;
         ctx->rpad = 0;
      }

      if (ctx->lpad) {
         itoaN(ctx->lpad, ctx->intbuf);
         ctx->lpad = 0;
         write_str(ctx, 'd', ctx->intbuf);
      }

      WRITE_CHAR(*ctx->fmt);
      goto end_sequence;
   }

   /* Make `break` unusable in the switch, in order to avoid confusion */
   NOT_REACHED();

end_sequence:
   snprintk_ctx_reset_state(ctx);
   return true;

truncated_seq:
out_of_dest_buffer:
   return false;
}

int vsnprintk(char *initial_buf, size_t size, const char *__fmt, va_list __args)
{
   struct snprintk_ctx __ctx;

   /* ctx has to be a pointer because of macros */
   struct snprintk_ctx *ctx = &__ctx;
   snprintk_ctx_reset_state(ctx);
   ctx->fmt = __fmt;
   ctx->buf = initial_buf;
   ctx->buf_end = initial_buf + size;
   va_copy(ctx->args, __args);

   for (char fmtX; (fmtX = *ctx->fmt); ctx->fmt++) {

      if (LIKELY(fmtX != '%')) {

         /* Regular character: just write it */
         WRITE_CHAR(fmtX);

      } else {

         /* fmt is '%': move it forward */
         ctx->fmt++;

         /* process the whole '%' sequence (it's often longer than 1 char) */
         if (!process_seq(ctx))
            break;   /* our dest is full or fmt finished early */
      }
   }

truncated_seq:
out_of_dest_buffer:
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
