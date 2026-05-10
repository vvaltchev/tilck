/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Userspace port of the kernel's ptype dump callbacks (the
 * formatter halves of modules/tracing/tracing_types.c,
 * ptype_buffer.c, ptype_iov.c) plus the errno + signal name
 * lookup tables that used to live in modules/tracing/.
 *
 * The save halves (which use copy_from_user) stay kernel-side;
 * by the time we read events from /syst/tracing/events the user-
 * pointer data has already been captured into the saved_params
 * area of struct dp_trace_event. Our job is to format those
 * captured bytes into the same colored ANSI strings master
 * produced.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <tilck/common/tracing/wire.h>

#include "tr.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/* On i386 long is 32-bit; on x86_64/riscv64 it's 64-bit. Use %ld
 * with a `long` value and the toolchain prints the right width on
 * each arch — same end result as the kernel's NBITS-conditional
 * format pick, just simpler. */

/* ----------------------- runtime state -------------------------- */

static bool g_force_exp_block;
static bool g_dump_big_bufs;

void tr_set_force_exp_block(bool v) { g_force_exp_block = v; }
void tr_set_dump_big_bufs(bool v)   { g_dump_big_bufs = v; }
bool tr_get_force_exp_block(void)   { return g_force_exp_block; }
bool tr_get_dump_big_bufs(void)     { return g_dump_big_bufs; }

/* ----------------------- errno + signal names -------------------- */

/* Verbatim port of modules/tracing/errno_names.c. Keep in sync if
 * the kernel-side table is updated. */
static const char *errno_names[] = {
   [EPERM] = "EPERM",         [ENOENT] = "ENOENT",
   [ESRCH] = "ESRCH",         [EINTR]  = "EINTR",
   [EIO]   = "EIO",           [ENXIO]  = "ENXIO",
   [E2BIG] = "E2BIG",         [ENOEXEC]= "ENOEXEC",
   [EBADF] = "EBADF",         [ECHILD] = "ECHILD",
   [EAGAIN]= "EAGAIN",        [ENOMEM] = "ENOMEM",
   [EACCES]= "EACCES",        [EFAULT] = "EFAULT",
   [ENOTBLK]="ENOTBLK",       [EBUSY]  = "EBUSY",
   [EEXIST]= "EEXIST",        [EXDEV]  = "EXDEV",
   [ENODEV]= "ENODEV",        [ENOTDIR]= "ENOTDIR",
   [EISDIR]= "EISDIR",        [EINVAL] = "EINVAL",
   [ENFILE]= "ENFILE",        [EMFILE] = "EMFILE",
   [ENOTTY]= "ENOTTY",        [ETXTBSY]= "ETXTBSY",
   [EFBIG] = "EFBIG",         [ENOSPC] = "ENOSPC",
   [ESPIPE]= "ESPIPE",        [EROFS]  = "EROFS",
   [EMLINK]= "EMLINK",        [EPIPE]  = "EPIPE",
   [EDOM]  = "EDOM",          [ERANGE] = "ERANGE",
   [EDEADLK]="EDEADLK",       [ENAMETOOLONG]="ENAMETOOLONG",
   [ENOLCK]= "ENOLCK",        [ENOSYS] = "ENOSYS",
   [ENOTEMPTY]="ENOTEMPTY",   [ELOOP]  = "ELOOP",
   [ENOMSG]= "ENOMSG",        [EIDRM]  = "EIDRM",
   [ECHRNG]= "ECHRNG",        [EL2NSYNC]="EL2NSYNC",
   [EL3HLT]= "EL3HLT",        [EL3RST] = "EL3RST",
   [ELNRNG]= "ELNRNG",        [EUNATCH]= "EUNATCH",
   [ENOCSI]= "ENOCSI",        [EL2HLT] = "EL2HLT",
   [EBADE] = "EBADE",         [EBADR]  = "EBADR",
   [EXFULL]= "EXFULL",        [ENOANO] = "ENOANO",
   [EBADRQC]="EBADRQC",       [EBADSLT]= "EBADSLT",
   [EBFONT]= "EBFONT",        [ENOSTR] = "ENOSTR",
   [ENODATA]="ENODATA",       [ETIME]  = "ETIME",
   [ENOSR] = "ENOSR",         [ENONET] = "ENONET",
   [ENOPKG]= "ENOPKG",        [EREMOTE]= "EREMOTE",
   [ENOLINK]="ENOLINK",       [EADV]   = "EADV",
   [ESRMNT]= "ESRMNT",        [ECOMM]  = "ECOMM",
   [EPROTO]= "EPROTO",        [EMULTIHOP]="EMULTIHOP",
   [EDOTDOT]="EDOTDOT",       [EBADMSG]= "EBADMSG",
   [EOVERFLOW]="EOVERFLOW",   [ENOTUNIQ]="ENOTUNIQ",
   [EBADFD]= "EBADFD",        [EREMCHG]= "EREMCHG",
   [ELIBACC]="ELIBACC",       [ELIBBAD]= "ELIBBAD",
   [ELIBSCN]="ELIBSCN",       [ELIBMAX]= "ELIBMAX",
   [ELIBEXEC]="ELIBEXEC",     [EILSEQ] = "EILSEQ",
   [ERESTART]="ERESTART",     [ESTRPIPE]="ESTRPIPE",
   [EUSERS]= "EUSERS",        [ENOTSOCK]="ENOTSOCK",
   [EDESTADDRREQ]="EDESTADDRREQ", [EMSGSIZE]="EMSGSIZE",
   [EPROTOTYPE]="EPROTOTYPE", [ENOPROTOOPT]="ENOPROTOOPT",
   [EPROTONOSUPPORT]="EPROTONOSUPPORT",
   [ESOCKTNOSUPPORT]="ESOCKTNOSUPPORT",
   [EOPNOTSUPP]="EOPNOTSUPP", [EPFNOSUPPORT]="EPFNOSUPPORT",
   [EAFNOSUPPORT]="EAFNOSUPPORT",
   [EADDRINUSE]="EADDRINUSE", [EADDRNOTAVAIL]="EADDRNOTAVAIL",
   [ENETDOWN]="ENETDOWN",     [ENETUNREACH]="ENETUNREACH",
   [ENETRESET]="ENETRESET",
   [ECONNABORTED]="ECONNABORTED",
   [ECONNRESET]="ECONNRESET", [ENOBUFS]="ENOBUFS",
   [EISCONN]="EISCONN",       [ENOTCONN]="ENOTCONN",
   [ESHUTDOWN]="ESHUTDOWN",   [ETOOMANYREFS]="ETOOMANYREFS",
   [ETIMEDOUT]="ETIMEDOUT",   [ECONNREFUSED]="ECONNREFUSED",
   [EHOSTDOWN]="EHOSTDOWN",   [EHOSTUNREACH]="EHOSTUNREACH",
   [EALREADY]="EALREADY",     [EINPROGRESS]="EINPROGRESS",
   [ESTALE]="ESTALE",         [EUCLEAN]="EUCLEAN",
   [ENOTNAM]="ENOTNAM",       [ENAVAIL]="ENAVAIL",
   [EISNAM]="EISNAM",         [EREMOTEIO]="EREMOTEIO",
   [EDQUOT]="EDQUOT",         [ENOMEDIUM]="ENOMEDIUM",
   [EMEDIUMTYPE]="EMEDIUMTYPE", [ECANCELED]="ECANCELED",
   [ENOKEY]="ENOKEY",         [EKEYEXPIRED]="EKEYEXPIRED",
   [EKEYREVOKED]="EKEYREVOKED", [EKEYREJECTED]="EKEYREJECTED",
   [EOWNERDEAD]="EOWNERDEAD",
   [ENOTRECOVERABLE]="ENOTRECOVERABLE",
   [ERFKILL]="ERFKILL",       [EHWPOISON]="EHWPOISON",
};

const char *tr_get_errno_name(int err)
{
   if (err <= 0 || err >= (int)(sizeof(errno_names)/sizeof(errno_names[0])))
      return "?";

   const char *n = errno_names[err];
   return n ? n : "?";
}

/* Verbatim port of modules/tracing/tracing.c::get_signal_name. */
const char *tr_get_signal_name(int signum)
{
   static const char *sig_names[NSIG] = {
      [SIGHUP]    = "SIGHUP",    [SIGINT]    = "SIGINT",
      [SIGQUIT]   = "SIGQUIT",   [SIGILL]    = "SIGILL",
      [SIGABRT]   = "SIGABRT",   [SIGFPE]    = "SIGFPE",
      [SIGKILL]   = "SIGKILL",   [SIGSEGV]   = "SIGSEGV",
      [SIGPIPE]   = "SIGPIPE",   [SIGALRM]   = "SIGALRM",
      [SIGTERM]   = "SIGTERM",   [SIGUSR1]   = "SIGUSR1",
      [SIGUSR2]   = "SIGUSR2",   [SIGCHLD]   = "SIGCHLD",
      [SIGCONT]   = "SIGCONT",   [SIGSTOP]   = "SIGSTOP",
      [SIGTSTP]   = "SIGTSTP",   [SIGTTIN]   = "SIGTTIN",
      [SIGTTOU]   = "SIGTTOU",   [SIGBUS]    = "SIGBUS",
      [SIGPOLL]   = "SIGPOLL",   [SIGPROF]   = "SIGPROF",
      [SIGSYS]    = "SIGSYS",    [SIGTRAP]   = "SIGTRAP",
      [SIGURG]    = "SIGURG",    [SIGVTALRM] = "SIGVTALRM",
      [SIGXCPU]   = "SIGXCPU",   [SIGXFSZ]   = "SIGXFSZ",
      [SIGPWR]    = "SIGPWR",    [SIGWINCH]  = "SIGWINCH",
   };

   if (signum < 0 || signum >= (int)(sizeof(sig_names)/sizeof(sig_names[0])))
      return "";

   return sig_names[signum] ? sig_names[signum] : "";
}

/* ============================ dump callbacks ====================== */
/*
 * Each callback returns false iff dest_buf_size was too small to
 * contain the formatted value (mirrors the kernel ptype contract).
 * The renderer falls back to "(raw) %p" when that happens.
 */

/* ------------------------- simple integer ------------------------- */

static bool dump_int(unsigned long uval, long h, char *dst, size_t bs)
{
   (void)h;
   const long val = (long)uval;
   const int rc = snprintf(dst, bs, "%ld", val);
   return rc >= 0 && (size_t)rc < bs;
}

static bool dump_voidp(unsigned long val, long h, char *dst, size_t bs)
{
   (void)h;
   const int rc = (val != 0)
      ? snprintf(dst, bs, "%p", (void *)val)
      : snprintf(dst, bs, "NULL");
   return rc >= 0 && (size_t)rc < bs;
}

static bool dump_oct(unsigned long val, long h, char *dst, size_t bs)
{
   (void)h;
   const int rc = snprintf(dst, bs, "0%03o", (int)val);
   return rc >= 0 && (size_t)rc < bs;
}

static bool dump_errno_or_val(unsigned long uval, long h, char *dst, size_t bs)
{
   (void)h;
   const int val = (int)uval;
   const int rc = (val >= 0)
      ? snprintf(dst, bs, "%d", val)
      : snprintf(dst, bs, "-%s", tr_get_errno_name(-val));
   return rc >= 0 && (size_t)rc < bs;
}

static bool dump_errno_or_ptr(unsigned long uval, long h, char *dst, size_t bs)
{
   (void)h;
   const long val = (long)uval;
   const int rc = (val >= 0 || val < -500 /* smallest errno */)
      ? snprintf(dst, bs, "%p", (void *)uval)
      : snprintf(dst, bs, "-%s", tr_get_errno_name((int)-val));
   return rc >= 0 && (size_t)rc < bs;
}

/* ------------------------- open flags ----------------------------- */

static bool buf_append(char *dst, int *used, int *rem, const char *str)
{
   if (*rem <= 0)
      return false;

   const int n = snprintf(dst + *used, (size_t)*rem, "%s", str);

   if (n < 0 || n >= *rem)
      return false;

   *used += n;
   *rem -= n;
   return true;
}

#define OPEN_CHECK_FLAG(_x)                                  \
   do {                                                      \
      if (((fl) & (_x)) == (_x))                             \
         if (!buf_append(dst, &used, &rem, #_x "|"))         \
            return false;                                    \
   } while (0)

static bool dump_open_flags(unsigned long fl, long h, char *dst, size_t bs)
{
   (void)h;
   int rem = (int)bs;
   int used = 0;

   if (fl == 0) {

      if (bs < 2)
         return false;

      memcpy(dst, "0", 2);
      return true;
   }

   OPEN_CHECK_FLAG(O_APPEND);
   OPEN_CHECK_FLAG(O_ASYNC);
   OPEN_CHECK_FLAG(O_CLOEXEC);
   OPEN_CHECK_FLAG(O_CREAT);
#ifdef O_DIRECT
   OPEN_CHECK_FLAG(O_DIRECT);
#endif
   OPEN_CHECK_FLAG(O_DIRECTORY);
   OPEN_CHECK_FLAG(O_DSYNC);
   OPEN_CHECK_FLAG(O_EXCL);
#ifdef O_LARGEFILE
   OPEN_CHECK_FLAG(O_LARGEFILE);
#endif
#ifdef O_NOATIME
   OPEN_CHECK_FLAG(O_NOATIME);
#endif
   OPEN_CHECK_FLAG(O_NOCTTY);
   OPEN_CHECK_FLAG(O_NOFOLLOW);
   OPEN_CHECK_FLAG(O_NONBLOCK);
   OPEN_CHECK_FLAG(O_NDELAY);
#ifdef O_PATH
   OPEN_CHECK_FLAG(O_PATH);
#endif
   OPEN_CHECK_FLAG(O_SYNC);
#ifdef O_TMPFILE
   OPEN_CHECK_FLAG(O_TMPFILE);
#endif
   OPEN_CHECK_FLAG(O_TRUNC);

   if (used > 0 && dst[used - 1] == '|')
      dst[used - 1] = '\0';

   return true;
}

/* ------------------------- doff64 / whence / int_pair / u64_ptr --- */

static bool dump_doff64(unsigned long hi, long h, char *dst, size_t bs)
{
   const unsigned long low = (unsigned long)h;
   const unsigned long long val = ((unsigned long long)hi << 32) |
                                  (unsigned long long)low;
   const int rc = snprintf(dst, bs, "%llu", val);
   return rc >= 0 && (size_t)rc < bs;
}

static bool dump_whence(unsigned long val, long h, char *dst, size_t bs)
{
   (void)h;
   const char *s;

   switch ((int)val) {
      case SEEK_SET: s = "SEEK_SET"; break;
      case SEEK_CUR: s = "SEEK_CUR"; break;
      case SEEK_END: s = "SEEK_END"; break;
      default:       s = NULL;       break;
   }

   const int rc = s
      ? snprintf(dst, bs, "%s", s)
      : snprintf(dst, bs, "unknown: %d", (int)val);
   return rc >= 0 && (size_t)rc < bs;
}

/* Mirrors struct saved_int_pair_data layout from kernel
 * tracing_types.c. The kernel save callback writes this into the
 * slot; we read the same bytes here. */
struct tr_saved_int_pair {
   bool valid;
   int  pair[2];
};

static bool dump_int_pair_with_data(unsigned long orig,
                                    char *data,
                                    long unused1,
                                    long unused2,
                                    char *dst, size_t bs)
{
   (void)orig; (void)unused1; (void)unused2;
   const struct tr_saved_int_pair *d = (const void *)data;

   const int rc = !d->valid
      ? snprintf(dst, bs, "<fault>")
      : snprintf(dst, bs, "{%d, %d}", d->pair[0], d->pair[1]);
   return rc >= 0 && (size_t)rc <= bs;
}

/* The kernel's save_param_u64_ptr wrote a stringified u64 directly
 * into the slot; the dump callback just memcpy's it out. */
static bool dump_u64_ptr_with_data(unsigned long orig,
                                   char *data,
                                   long unused1,
                                   long unused2,
                                   char *dst, size_t bs)
{
   (void)orig; (void)unused1; (void)unused2;

   const size_t len = strnlen(data, bs);

   if (len + 1 >= bs)
      return false;

   memcpy(dst, data, len + 1);
   return true;
}

/* ------------------------- signum --------------------------------- */

static bool dump_signum(unsigned long val, long h, char *dst, size_t bs)
{
   (void)h;
   const int signum = (int)val;
   const int rc = snprintf(dst, bs, "%d [%s]",
                           signum, tr_get_signal_name(signum));
   return rc >= 0 && (size_t)rc < bs;
}

/* ------------------------- buffer (and big_buf, path) ------------- */

/*
 * Mirrors modules/tracing/ptype_buffer.c::dump_param_buffer with two
 * differences: (1) snprintk → snprintf, (2)
 * tracing_are_dump_big_bufs_on() reads our local flag.
 */
static bool dump_buffer_with_data(unsigned long orig,
                                  char *data,
                                  long data_bs,
                                  long real_sz,
                                  char *dst, size_t bs)
{
   if (bs <= 8)
      return false;

   if (!orig) {
      const int rc = snprintf(dst, bs, "NULL");
      return rc >= 0 && (size_t)rc < bs;
   }

   if (data_bs == -1)
      data_bs = (long)strlen(data);

   if (!g_dump_big_bufs && real_sz > 0)
      real_sz = MIN(real_sz, 16);

   char minibuf[8];
   const char *s;
   const char *data_end = data + (real_sz < 0 ? data_bs : MIN(real_sz, data_bs));
   char *dest_end = dst + bs;

   *dst++ = '\"';

   for (s = data; s < data_end; s++) {

      char c = *s;
      long ml = 0;

      switch (c) {
         case '\n': memcpy(minibuf, "\\n", 3); break;
         case '\r': memcpy(minibuf, "\\r", 3); break;
         case '\"': memcpy(minibuf, "\\\"", 3); break;
         case '\\': memcpy(minibuf, "\\\\", 3); break;
         default:
            if (isprint((unsigned char)c)) {
               minibuf[0] = c;
               minibuf[1] = '\0';
            } else {
               snprintf(minibuf, sizeof(minibuf),
                        "\\x%x", (unsigned char)c);
            }
      }

      ml = (long)strlen(minibuf);

      if (dest_end - dst < ml - 1) {
         dst = dest_end;
         break;
      }

      memcpy(dst, minibuf, (size_t)ml);
      dst += ml;
   }

   if (dst >= dest_end - 4) {

      dst[-1] = '\0';
      dst[-2] = '\"';
      dst[-3] = '.';
      dst[-4] = '.';
      dst[-5] = '.';

   } else {

      if (s == data_end && real_sz > 0 && data_bs < real_sz) {
         *dst++ = '.';
         *dst++ = '.';
         *dst++ = '.';
      }

      *dst++ = '\"';
      *dst   = '\0';
   }

   return true;
}

/* ------------------------- iov ------------------------------------ */

/*
 * The kernel save_param_iov packs up to 4 iovec descriptors into the
 * 128-byte slot: the first 32 bytes are 4 longs (iov_len), then 4
 * ulongs (iov_base) at +32, then 4 × 16-byte mini-buffers at +64.
 * We mirror the same layout when reading back.
 */
static bool dump_iov_inner(unsigned long orig,
                           char *data,
                           long u_iovcnt,
                           long maybe_tot_data_size,
                           char *dst, size_t bs)
{
   (void)orig;
   int used = 0, rem = (int)bs;
   long iovcnt = MIN(u_iovcnt, 4);
   long tot_rem = maybe_tot_data_size >= 0 ? maybe_tot_data_size : 16;
   char buf[32];

   if (bs < 128)
      return false;

   snprintf(buf, sizeof(buf), "(struct iovec[%ld]) {\r\n", u_iovcnt);

   if (!buf_append(dst, &used, &rem, buf))
      return false;

   for (int i = 0; i < iovcnt; i++) {

      const long len   = ((long *)         (void *)(data + 0))[i];
      const unsigned long base =
                          ((unsigned long *)(void *)(data + 32))[i];

      if (!buf_append(dst, &used, &rem, "   {base: "))
         return false;

      const bool ok = dump_buffer_with_data(
         base,
         data + 64 + i * 16,
         MIN(len, 16),
         maybe_tot_data_size >= 0 ? MIN(tot_rem, len) : len,
         buf,
         sizeof(buf));

      if (!ok)
         return false;

      if (maybe_tot_data_size >= 0)
         tot_rem -= len;

      if (!buf_append(dst, &used, &rem, buf))
         return false;

      if (!buf_append(dst, &used, &rem, ", len: "))
         return false;

      snprintf(buf, sizeof(buf), "%ld", len);

      if (!buf_append(dst, &used, &rem, buf))
         return false;

      if (!buf_append(dst, &used, &rem,
                      i < u_iovcnt - 1 ? "}, \r\n" : "}"))
         return false;
   }

   if (u_iovcnt > iovcnt) {

      if (!buf_append(dst, &used, &rem, "... "))
         return false;
   }

   if (!buf_append(dst, &used, &rem, "\r\n}"))
      return false;

   return true;
}

static bool dump_iov_in_with_data(unsigned long orig,
                                  char *data,
                                  long u_iovcnt,
                                  long unused,
                                  char *dst, size_t bs)
{
   (void)unused;
   return dump_iov_inner(orig, data, u_iovcnt, -1, dst, bs);
}

static bool dump_iov_out_with_data(unsigned long orig,
                                   char *data,
                                   long u_iovcnt,
                                   long real_sz,
                                   char *dst, size_t bs)
{
   return dump_iov_inner(orig, data, u_iovcnt, real_sz, dst, bs);
}

/* ============================ dispatch ============================ */

bool tr_dump_from_val(unsigned type_id,
                      unsigned long val,
                      long helper,
                      char *dst, size_t bs)
{
   switch (type_id) {

      case TR_PT_INT:           return dump_int(val, helper, dst, bs);
      case TR_PT_VOIDP:         return dump_voidp(val, helper, dst, bs);
      case TR_PT_OCT:           return dump_oct(val, helper, dst, bs);
      case TR_PT_ERRNO_OR_VAL:  return dump_errno_or_val(val, helper, dst, bs);
      case TR_PT_ERRNO_OR_PTR:  return dump_errno_or_ptr(val, helper, dst, bs);
      case TR_PT_OPEN_FLAGS:    return dump_open_flags(val, helper, dst, bs);
      case TR_PT_DOFF64:        return dump_doff64(val, helper, dst, bs);
      case TR_PT_WHENCE:        return dump_whence(val, helper, dst, bs);
      case TR_PT_SIGNUM:        return dump_signum(val, helper, dst, bs);

      /* These ptypes only have a `dump` (with-data) variant in the
       * kernel — no dump_from_val. Mirror that. */
      case TR_PT_INT32_PAIR:
      case TR_PT_U64_PTR:
      case TR_PT_BUFFER:
      case TR_PT_BIG_BUF:
      case TR_PT_PATH:
      case TR_PT_IOV_IN:
      case TR_PT_IOV_OUT:
      default:
         return false;
   }
}

bool tr_dump(unsigned type_id,
             unsigned long orig,
             char *data, long data_size,
             long helper,
             char *dst, size_t bs)
{
   switch (type_id) {

      case TR_PT_INT32_PAIR:
         return dump_int_pair_with_data(orig, data, data_size, helper,
                                        dst, bs);

      case TR_PT_U64_PTR:
         return dump_u64_ptr_with_data(orig, data, data_size, helper,
                                       dst, bs);

      case TR_PT_BUFFER:
      case TR_PT_BIG_BUF:
      case TR_PT_PATH:
         return dump_buffer_with_data(orig, data, data_size, helper,
                                      dst, bs);

      case TR_PT_IOV_IN:
         return dump_iov_in_with_data(orig, data, data_size, helper,
                                      dst, bs);

      case TR_PT_IOV_OUT:
         return dump_iov_out_with_data(orig, data, data_size, helper,
                                       dst, bs);

      /* These ptypes have only dump_from_val — no with-data path.
       * Mirror that. */
      case TR_PT_INT:
      case TR_PT_VOIDP:
      case TR_PT_OCT:
      case TR_PT_ERRNO_OR_VAL:
      case TR_PT_ERRNO_OR_PTR:
      case TR_PT_OPEN_FLAGS:
      case TR_PT_DOFF64:
      case TR_PT_WHENCE:
      case TR_PT_SIGNUM:
      default:
         return false;
   }
}
