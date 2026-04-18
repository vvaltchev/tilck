/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Shim for <elf.h>.
 * On Linux, forward to the real header. On other platforms, use Tilck's
 * own ELF definitions from include/3rd_party/elf.h.
 */

#pragma once

#ifdef __linux__
#include_next <elf.h>
#else
#include <3rd_party/elf.h>
#endif
