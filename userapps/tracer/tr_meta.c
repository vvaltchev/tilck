/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Loads /syst/tracing/metadata once at tracer startup, validates the
 * wire format, and builds an in-process by-sys_n index for the
 * renderer.
 *
 * The metadata is static for the lifetime of the kernel — it's built
 * once at MOD_tracing init from the populated syscalls_info +
 * params_slots + syscalls_fmts arrays. So we read it once and keep a
 * heap-allocated copy.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <tilck/common/tracing/wire.h>

#include "tr.h"

#define META_PATH      "/syst/tracing/metadata"
#define MAX_SYS_N      512   /* cap for the by-sys_n index */

static char *meta_blob;
static size_t meta_blob_size;

static const struct tr_wire_header *meta_hdr;
static const struct tr_wire_ptype_info *meta_ptypes;
static const struct tr_wire_syscall *meta_syscalls;

/* Sparse lookup by sys_n; NULL entries mean "no metadata for this
 * syscall, render name-only". */
static const struct tr_wire_syscall *by_sys_n[MAX_SYS_N];

static bool inited;

/*
 * read() the entire metadata file into a heap buffer. Returns 0 on
 * success or -errno. We size the buffer up front via fstat so a
 * single read() suffices for any reasonable blob size; if the kernel
 * later grows this past a few MB we'd want to switch to mmap.
 */
static int load_meta_file(void)
{
   int fd = open(META_PATH, O_RDONLY);

   if (fd < 0)
      return -errno;

   /* fstat-then-read keeps the loader simple; if the immutable
    * sysfs file ever grows past what one read() can handle the
    * caller below loops, but in practice it's tens of KB. */
   off_t end = lseek(fd, 0, SEEK_END);

   if (end < 0) {
      int e = errno;
      close(fd);
      return -e;
   }

   if (lseek(fd, 0, SEEK_SET) < 0) {
      int e = errno;
      close(fd);
      return -e;
   }

   if (end < (off_t)sizeof(struct tr_wire_header)) {
      close(fd);
      return -EINVAL;
   }

   meta_blob = malloc((size_t)end);

   if (!meta_blob) {
      close(fd);
      return -ENOMEM;
   }

   size_t total = 0;

   while (total < (size_t)end) {

      ssize_t n = read(fd, meta_blob + total, (size_t)end - total);

      if (n == 0)
         break;     /* short file; treat as load error */

      if (n < 0 && errno == EINTR)
         continue;

      if (n < 0) {
         int e = errno;
         close(fd);
         free(meta_blob);
         meta_blob = NULL;
         return -e;
      }

      total += (size_t)n;
   }

   close(fd);

   if (total < sizeof(struct tr_wire_header)) {
      free(meta_blob);
      meta_blob = NULL;
      return -EINVAL;
   }

   meta_blob_size = total;
   return 0;
}

/*
 * Validate header magic + version + counts, set up internal pointers,
 * populate by_sys_n[]. On any inconsistency, free the blob and leave
 * the loader in "no metadata" state — the renderer falls back to
 * name-only events.
 */
static int validate_and_index(void)
{
   meta_hdr = (const struct tr_wire_header *)meta_blob;

   if (meta_hdr->magic != TR_WIRE_MAGIC)
      return -EINVAL;

   if (meta_hdr->version != TR_WIRE_VERSION)
      return -ENOTSUP;

   const size_t need =
      sizeof(struct tr_wire_header)
      + (size_t)meta_hdr->ptype_count * sizeof(struct tr_wire_ptype_info)
      + (size_t)meta_hdr->syscall_count * sizeof(struct tr_wire_syscall);

   if (need > meta_blob_size)
      return -EINVAL;

   meta_ptypes   = (const void *)(meta_hdr + 1);
   meta_syscalls = (const void *)(meta_ptypes + meta_hdr->ptype_count);

   for (unsigned i = 0; i < meta_hdr->syscall_count; i++) {

      unsigned sn = meta_syscalls[i].sys_n;

      if (sn < MAX_SYS_N)
         by_sys_n[sn] = &meta_syscalls[i];
   }

   return 0;
}

int tr_meta_init(void)
{
   if (inited)
      return 0;

   int rc = load_meta_file();

   if (rc < 0) {
      fprintf(stderr,
              "dp: warning: cannot load %s: errno=%d (renderer will "
              "fall back to name-only events)\r\n",
              META_PATH, -rc);
      inited = true;
      return rc;
   }

   rc = validate_and_index();

   if (rc < 0) {
      fprintf(stderr,
              "dp: warning: %s has bad header: rc=%d (renderer will "
              "fall back to name-only events)\r\n",
              META_PATH, rc);

      free(meta_blob);
      meta_blob = NULL;
      meta_hdr = NULL;
      meta_ptypes = NULL;
      meta_syscalls = NULL;
      inited = true;
      return rc;
   }

   inited = true;
   return 0;
}

const struct tr_wire_syscall *tr_get_sys_info(unsigned sys_n)
{
   if (sys_n >= MAX_SYS_N)
      return NULL;

   return by_sys_n[sys_n];
}

const struct tr_wire_ptype_info *tr_get_ptype_info(unsigned type_id)
{
   if (!meta_hdr || type_id >= meta_hdr->ptype_count)
      return NULL;

   return &meta_ptypes[type_id];
}
