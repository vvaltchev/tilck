// SPDX-License-Identifier: BSD-2-Clause
/*
 * fdt_helper.h - Flat Device Tree parsing helper routines
 * Implement helper routines to parse FDT nodes on top of
 * libfdt for OpenSBI usage
 *
 * Copyright (C) 2020 Bin Meng <bmeng.cn@gmail.com>
 */

#ifndef __FDT_HELPER_H__
#define __FDT_HELPER_H__

#include <tilck/common/basic_defs.h>

struct fdt_match {
   const char *compatible;
   const void *data;
};

#define FDT_MAX_PHANDLE_ARGS 16
struct fdt_phandle_args {
   int node_offset;
   int args_count;
   u32 args[FDT_MAX_PHANDLE_ARGS];
};

const struct fdt_match *fdt_match_node(void *fdt, int nodeoff,
                   const struct fdt_match *match_table);

int fdt_find_match(void *fdt, int startoff,
         const struct fdt_match *match_table,
         const struct fdt_match **out_match);

int fdt_parse_phandle_with_args(void *fdt, int nodeoff,
            const char *prop, const char *cells_prop,
            int index, struct fdt_phandle_args *out_args);

int fdt_get_node_addr_size(void *fdt, int node, int index,
            uint64_t *addr, uint64_t *size);

int fdt_get_node_addr_size_by_name(void *fdt, int node, const char *name,
               uint64_t *addr, uint64_t *size);

bool fdt_node_is_enabled(void *fdt, int nodeoff);

#endif /* __FDT_HELPER_H__ */
