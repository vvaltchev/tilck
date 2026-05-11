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

/* ============================ Layer 1 — symbolic ptypes ===========
 * Each callback formats a register-value integer as a symbolic
 * bitmask or enum string. Falls back to "0xNNNN" for unknown
 * values so the trace stays useful when the kernel adds new
 * values that userspace hasn't been updated for.
 *
 * Pattern matches the existing dump_open_flags / dump_signum /
 * dump_whence helpers earlier in the file.
 * ================================================================== */

#include <sys/mman.h>     /* PROT_*, MAP_* */
#include <sys/wait.h>     /* WNOHANG, WUNTRACED, WCONTINUED */
#include <sys/mount.h>    /* MS_* */

/* For ioctl/fcntl cmds we use small inline tables of (value, name)
 * pairs. Generic helper: scan a table, return the matching name or
 * NULL on miss. */
struct enum_pair { unsigned long value; const char *name; };

static const char *enum_lookup(const struct enum_pair *t, unsigned long v)
{
   for (; t->name; t++)
      if (t->value == v)
         return t->name;
   return NULL;
}

/* Bitmask helper. Walks a table, appends "NAME|" to dst for each
 * matching bit. Returns true if everything fit. */
static bool
bitmask_dump(const struct enum_pair *t, unsigned long v,
             char *dst, size_t bs)
{
   int used = 0, rem = (int)bs;
   bool any = false;
   unsigned long matched = 0;

   if (v == 0) {
      const int rc = snprintf(dst, bs, "0");
      return rc >= 0 && (size_t)rc < bs;
   }

   for (; t->name; t++) {

      if (t->value && (v & t->value) == t->value && !(matched & t->value)) {

         if (!buf_append(dst, &used, &rem, t->name))
            return false;
         if (!buf_append(dst, &used, &rem, "|"))
            return false;
         matched |= t->value;
         any = true;
      }
   }

   /* Any leftover bits not matched by the table → render as hex
    * trailing (e.g. "MAP_PRIVATE|MAP_ANONYMOUS|0x100000"). */
   const unsigned long rest = v & ~matched;
   if (rest) {
      char hex[16];
      snprintf(hex, sizeof(hex), "0x%lx", rest);
      if (!buf_append(dst, &used, &rem, hex))
         return false;
      any = true;
   } else if (any) {
      /* Drop the trailing '|' */
      if (used > 0 && dst[used - 1] == '|')
         dst[used - 1] = '\0';
   }

   return true;
}

/* ----- mmap.prot ------------------------------------------------- */
static const struct enum_pair tab_mmap_prot[] = {
#ifdef PROT_READ
   { PROT_READ,  "PROT_READ"  },
#endif
#ifdef PROT_WRITE
   { PROT_WRITE, "PROT_WRITE" },
#endif
#ifdef PROT_EXEC
   { PROT_EXEC,  "PROT_EXEC"  },
#endif
   { 0, NULL }
};

static bool dump_mmap_prot(unsigned long v, long h, char *dst, size_t bs)
{
   (void)h;
   if (v == 0) {
      int rc = snprintf(dst, bs, "PROT_NONE");
      return rc >= 0 && (size_t)rc < bs;
   }
   return bitmask_dump(tab_mmap_prot, v, dst, bs);
}

/* ----- mmap.flags ------------------------------------------------ */
static const struct enum_pair tab_mmap_flags[] = {
#ifdef MAP_SHARED
   { MAP_SHARED,     "MAP_SHARED"     },
#endif
#ifdef MAP_PRIVATE
   { MAP_PRIVATE,    "MAP_PRIVATE"    },
#endif
#ifdef MAP_FIXED
   { MAP_FIXED,      "MAP_FIXED"      },
#endif
#ifdef MAP_ANONYMOUS
   { MAP_ANONYMOUS,  "MAP_ANONYMOUS"  },
#endif
#ifdef MAP_GROWSDOWN
   { MAP_GROWSDOWN,  "MAP_GROWSDOWN"  },
#endif
#ifdef MAP_DENYWRITE
   { MAP_DENYWRITE,  "MAP_DENYWRITE"  },
#endif
#ifdef MAP_EXECUTABLE
   { MAP_EXECUTABLE, "MAP_EXECUTABLE" },
#endif
#ifdef MAP_LOCKED
   { MAP_LOCKED,     "MAP_LOCKED"     },
#endif
#ifdef MAP_NORESERVE
   { MAP_NORESERVE,  "MAP_NORESERVE"  },
#endif
#ifdef MAP_POPULATE
   { MAP_POPULATE,   "MAP_POPULATE"   },
#endif
#ifdef MAP_NONBLOCK
   { MAP_NONBLOCK,   "MAP_NONBLOCK"   },
#endif
#ifdef MAP_STACK
   { MAP_STACK,      "MAP_STACK"      },
#endif
#ifdef MAP_HUGETLB
   { MAP_HUGETLB,    "MAP_HUGETLB"    },
#endif
   { 0, NULL }
};

static bool dump_mmap_flags(unsigned long v, long h, char *dst, size_t bs)
{
   (void)h;
   return bitmask_dump(tab_mmap_flags, v, dst, bs);
}

/* ----- waitpid options ------------------------------------------ */
static const struct enum_pair tab_wait_options[] = {
#ifdef WNOHANG
   { WNOHANG,    "WNOHANG"    },
#endif
#ifdef WUNTRACED
   { WUNTRACED,  "WUNTRACED"  },
#endif
#ifdef WCONTINUED
   { WCONTINUED, "WCONTINUED" },
#endif
   { 0, NULL }
};

static bool dump_wait_options(unsigned long v, long h, char *dst, size_t bs)
{
   (void)h;
   return bitmask_dump(tab_wait_options, v, dst, bs);
}

/* ----- access.mode (R_OK | W_OK | X_OK | F_OK) ------------------ */
static const struct enum_pair tab_access_mode[] = {
   { 4 /* R_OK */, "R_OK" },
   { 2 /* W_OK */, "W_OK" },
   { 1 /* X_OK */, "X_OK" },
   { 0, NULL }
};

static bool dump_access_mode(unsigned long v, long h, char *dst, size_t bs)
{
   (void)h;
   if (v == 0) {
      int rc = snprintf(dst, bs, "F_OK");
      return rc >= 0 && (size_t)rc < bs;
   }
   return bitmask_dump(tab_access_mode, v, dst, bs);
}

/* ----- ioctl.request --------------------------------------------- *
 * Tilck implements only a tty-shaped subset (see
 * kernel/tty/tty_ioctl.c). For unknown values we fall back to
 * "0xNNNN". Layer 2 will attach a struct-aware argp to this so
 * the renderer also dumps the pointee. */
static const struct enum_pair tab_ioctl_cmd[] = {
   { 0x5401, "TCGETS"      },
   { 0x5402, "TCSETS"      },
   { 0x5403, "TCSETSW"     },
   { 0x5404, "TCSETSF"     },
   { 0x540E, "TIOCSCTTY"   },
   { 0x540F, "TIOCGPGRP"   },
   { 0x5410, "TIOCSPGRP"   },
   { 0x5413, "TIOCGWINSZ"  },
   { 0x5414, "TIOCSWINSZ"  },
   { 0x5422, "TIOCNOTTY"   },
   { 0x4B33, "KDGKBTYPE"   },
   { 0x4B3A, "KDSETMODE"   },
   { 0x4B44, "KDGKBMODE"   },
   { 0x4B45, "KDSKBMODE"   },
   { 0, NULL }
};

static bool dump_ioctl_cmd(unsigned long v, long h, char *dst, size_t bs)
{
   (void)h;
   const char *name = enum_lookup(tab_ioctl_cmd, v);
   const int rc = name
      ? snprintf(dst, bs, "%s (0x%lx)", name, v)
      : snprintf(dst, bs, "0x%lx", v);
   return rc >= 0 && (size_t)rc < bs;
}

/* ----- fcntl.cmd -------------------------------------------------- */
static const struct enum_pair tab_fcntl_cmd[] = {
   { 0,  "F_DUPFD"          },
   { 1,  "F_GETFD"          },
   { 2,  "F_SETFD"          },
   { 3,  "F_GETFL"          },
   { 4,  "F_SETFL"          },
   { 5,  "F_GETLK"          },
   { 6,  "F_SETLK"          },
   { 7,  "F_SETLKW"         },
   { 1030, "F_DUPFD_CLOEXEC"},   /* musl numeric value */
   { 0, NULL }
};

static bool dump_fcntl_cmd(unsigned long v, long h, char *dst, size_t bs)
{
   (void)h;
   const char *name = enum_lookup(tab_fcntl_cmd, v);
   const int rc = name
      ? snprintf(dst, bs, "%s", name)
      : snprintf(dst, bs, "%lu", v);
   return rc >= 0 && (size_t)rc < bs;
}

/* ----- sigprocmask.how ------------------------------------------- */
static bool dump_sigprocmask_how(unsigned long v, long h, char *dst, size_t bs)
{
   (void)h;
   const char *name;
   switch (v) {
      case 0: name = "SIG_BLOCK";   break;
      case 1: name = "SIG_UNBLOCK"; break;
      case 2: name = "SIG_SETMASK"; break;
      default: name = NULL;
   }
   const int rc = name
      ? snprintf(dst, bs, "%s", name)
      : snprintf(dst, bs, "%lu", v);
   return rc >= 0 && (size_t)rc < bs;
}

/* ----- prctl.option ---------------------------------------------- */
static const struct enum_pair tab_prctl_option[] = {
   { 1,  "PR_SET_PDEATHSIG"   },
   { 2,  "PR_GET_PDEATHSIG"   },
   { 3,  "PR_GET_DUMPABLE"    },
   { 4,  "PR_SET_DUMPABLE"    },
   { 15, "PR_SET_NAME"        },
   { 16, "PR_GET_NAME"        },
   { 36, "PR_SET_CHILD_SUBREAPER" },
   { 37, "PR_GET_CHILD_SUBREAPER" },
   { 0, NULL }
};

static bool dump_prctl_option(unsigned long v, long h, char *dst, size_t bs)
{
   (void)h;
   const char *name = enum_lookup(tab_prctl_option, v);
   const int rc = name
      ? snprintf(dst, bs, "%s", name)
      : snprintf(dst, bs, "%lu", v);
   return rc >= 0 && (size_t)rc < bs;
}

/* ----- clone.flags ----------------------------------------------- */
static const struct enum_pair tab_clone_flags[] = {
   { 0x00000100, "CLONE_VM"        },
   { 0x00000200, "CLONE_FS"        },
   { 0x00000400, "CLONE_FILES"     },
   { 0x00000800, "CLONE_SIGHAND"   },
   { 0x00001000, "CLONE_PIDFD"     },
   { 0x00002000, "CLONE_PTRACE"    },
   { 0x00004000, "CLONE_VFORK"     },
   { 0x00008000, "CLONE_PARENT"    },
   { 0x00010000, "CLONE_THREAD"    },
   { 0x00020000, "CLONE_NEWNS"     },
   { 0x00040000, "CLONE_SYSVSEM"   },
   { 0x00080000, "CLONE_SETTLS"    },
   { 0x00100000, "CLONE_PARENT_SETTID" },
   { 0x00200000, "CLONE_CHILD_CLEARTID" },
   { 0x00400000, "CLONE_DETACHED"  },
   { 0x00800000, "CLONE_UNTRACED"  },
   { 0x01000000, "CLONE_CHILD_SETTID" },
   { 0, NULL }
};

static bool dump_clone_flags(unsigned long v, long h, char *dst, size_t bs)
{
   (void)h;

   /* The low byte of clone flags is actually the exit_signal sent
    * to the parent on child exit (e.g. SIGCHLD = 17). Render that
    * separately from the flag bits. */
   const unsigned long sig = v & 0xff;
   const unsigned long flags = v & ~0xfful;
   int used = 0, rem = (int)bs;

   if (sig) {
      char buf[24];
      snprintf(buf, sizeof(buf), "exit_signal=%lu", sig);
      if (!buf_append(dst, &used, &rem, buf))
         return false;
   }

   if (flags) {

      if (used && !buf_append(dst, &used, &rem, "|"))
         return false;

      char tmp[256];
      if (!bitmask_dump(tab_clone_flags, flags, tmp, sizeof(tmp)))
         return false;
      if (!buf_append(dst, &used, &rem, tmp))
         return false;
   }

   if (!used) {
      const int rc = snprintf(dst, bs, "0");
      return rc >= 0 && (size_t)rc < bs;
   }

   return true;
}

/* ----- mount.flags (MS_* bitmask) -------------------------------- */
static const struct enum_pair tab_mount_flags[] = {
#ifdef MS_RDONLY
   { MS_RDONLY,      "MS_RDONLY"      },
#endif
#ifdef MS_NOSUID
   { MS_NOSUID,      "MS_NOSUID"      },
#endif
#ifdef MS_NODEV
   { MS_NODEV,       "MS_NODEV"       },
#endif
#ifdef MS_NOEXEC
   { MS_NOEXEC,      "MS_NOEXEC"      },
#endif
#ifdef MS_SYNCHRONOUS
   { MS_SYNCHRONOUS, "MS_SYNCHRONOUS" },
#endif
#ifdef MS_REMOUNT
   { MS_REMOUNT,     "MS_REMOUNT"     },
#endif
#ifdef MS_MANDLOCK
   { MS_MANDLOCK,    "MS_MANDLOCK"    },
#endif
#ifdef MS_NOATIME
   { MS_NOATIME,     "MS_NOATIME"     },
#endif
#ifdef MS_NODIRATIME
   { MS_NODIRATIME,  "MS_NODIRATIME"  },
#endif
#ifdef MS_BIND
   { MS_BIND,        "MS_BIND"        },
#endif
   { 0, NULL }
};

static bool dump_mount_flags(unsigned long v, long h, char *dst, size_t bs)
{
   (void)h;
   return bitmask_dump(tab_mount_flags, v, dst, bs);
}

/* ----- madvise.advice -------------------------------------------- */
static const struct enum_pair tab_madvise_advice[] = {
   { 0,  "MADV_NORMAL"     },
   { 1,  "MADV_RANDOM"     },
   { 2,  "MADV_SEQUENTIAL" },
   { 3,  "MADV_WILLNEED"   },
   { 4,  "MADV_DONTNEED"   },
   { 8,  "MADV_FREE"       },
   { 9,  "MADV_REMOVE"     },
   { 10, "MADV_DONTFORK"   },
   { 11, "MADV_DOFORK"     },
   { 0, NULL }
};

static bool dump_madvise_advice(unsigned long v, long h, char *dst, size_t bs)
{
   (void)h;
   const char *name = enum_lookup(tab_madvise_advice, v);
   const int rc = name
      ? snprintf(dst, bs, "%s", name)
      : snprintf(dst, bs, "%lu", v);
   return rc >= 0 && (size_t)rc < bs;
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

      /* Layer 1 — symbolic register-value ptypes. */
      case TR_PT_MMAP_PROT:        return dump_mmap_prot(val, helper, dst, bs);
      case TR_PT_MMAP_FLAGS:       return dump_mmap_flags(val, helper, dst, bs);
      case TR_PT_WAIT_OPTIONS:     return dump_wait_options(val, helper, dst, bs);
      case TR_PT_ACCESS_MODE:      return dump_access_mode(val, helper, dst, bs);
      case TR_PT_IOCTL_CMD:        return dump_ioctl_cmd(val, helper, dst, bs);
      case TR_PT_FCNTL_CMD:        return dump_fcntl_cmd(val, helper, dst, bs);
      case TR_PT_SIGPROCMASK_HOW:  return dump_sigprocmask_how(val, helper, dst, bs);
      case TR_PT_PRCTL_OPTION:     return dump_prctl_option(val, helper, dst, bs);
      case TR_PT_CLONE_FLAGS:      return dump_clone_flags(val, helper, dst, bs);
      case TR_PT_MOUNT_FLAGS:      return dump_mount_flags(val, helper, dst, bs);
      case TR_PT_MADVISE_ADVICE:   return dump_madvise_advice(val, helper, dst, bs);

      /* Layer 2 — fcntl_arg. ptype_fcntl_arg has slot_size=0
       * (no struct capture), so the renderer reaches it here
       * rather than via tr_dump. Forward with empty data. */
      case TR_PT_FCNTL_ARG:        return tr_dump_fcntl_arg(val, NULL, 0,
                                                            helper, dst, bs);

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

      /* Layer 2 — context-dependent struct argp/arg. The dump
       * functions live in tr_dump_ioctl.c; helper is the cmd /
       * request value from the sibling param. */
      case TR_PT_IOCTL_ARGP:
         return tr_dump_ioctl_argp(orig, data, data_size, helper,
                                   dst, bs);

      case TR_PT_FCNTL_ARG:
         return tr_dump_fcntl_arg(orig, data, data_size, helper,
                                  dst, bs);

      /* Layer 3 — wstatus int decoded via the W*() macros. */
      case TR_PT_WSTATUS: {

         (void)data_size; (void)helper;

         if (!orig) {
            int rc = snprintf(dst, bs, "NULL");
            return rc >= 0 && (size_t)rc < bs;
         }

         const int s = *(const int *)data;
         char body[80] = {0};

         if (WIFEXITED(s)) {

            snprintf(body, sizeof(body),
                     "WIFEXITED, WEXITSTATUS=%d", WEXITSTATUS(s));

         } else if (WIFSIGNALED(s)) {

            snprintf(body, sizeof(body),
                     "WIFSIGNALED, WTERMSIG=%s[%d]",
                     tr_get_signal_name(WTERMSIG(s)), WTERMSIG(s));

         } else if (WIFSTOPPED(s)) {

            snprintf(body, sizeof(body),
                     "WIFSTOPPED, WSTOPSIG=%s[%d]",
                     tr_get_signal_name(WSTOPSIG(s)), WSTOPSIG(s));

#ifdef WIFCONTINUED
         } else if (WIFCONTINUED(s)) {

            snprintf(body, sizeof(body), "WIFCONTINUED");
#endif
         } else {

            snprintf(body, sizeof(body), "raw");
         }

         int rc = snprintf(dst, bs, "0x%04x [%s]", s, body);
         return rc >= 0 && (size_t)rc < bs;
      }

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
