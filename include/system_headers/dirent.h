/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Shim for <dirent.h>.
 * On FreeBSD, <dirent.h> defines `#define d_ino d_fileno` for backward
 * compatibility.  That macro rewrites member accesses on Tilck's own
 * struct linux_dirent64 (whose member is literally named d_ino),
 * causing compile errors.  Undo the macro after including the real
 * header — the Tilck kernel never uses FreeBSD's struct dirent.
 */

#pragma once

#include_next <dirent.h>

#ifdef __FreeBSD__
#undef d_ino
#endif
