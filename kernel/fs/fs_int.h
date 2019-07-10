/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/fs/vfs.h>

#define MP_CURSOR_SIZE_PTRS 1

/* Opaque mountpoint cursor type */
typedef struct {
   uptr data[MP_CURSOR_SIZE_PTRS];
} mp_cursor;


#ifndef _TILCK_MP_C_
void mountpoint_iter_begin(mp_cursor *c);
void mountpoint_iter_end(mp_cursor *c);
mountpoint *mountpoint_get_next(mp_cursor *c);
#endif

////////////////////////////////////////

typedef struct {

   const char *orig_path;       /* original path (used for offsets) */
   vfs_path paths[32];          /* paths stack */
   int ss;                      /* stack size */
   bool exlock;                 /* true -> use exlock, false -> use shlock */

} vfs_resolve_int_ctx;

STATIC int
__vfs_resolve_stack_push(vfs_resolve_int_ctx *ctx,
                         const char *path,
                         const vfs_path *p);
