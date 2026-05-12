/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * MemChunks panel: shows the size/count distribution of allocations
 * the kmalloc allocator has seen, plus per-row max-waste estimates.
 * Driven by TILCK_CMD_DP_GET_KMALLOC_CHUNKS, which only succeeds when
 * the kernel was built with KRN_KMALLOC_HEAVY_STATS=1; otherwise the
 * syscall returns -EOPNOTSUPP and we render a "not available" banner
 * to mirror what the in-kernel modules/debugpanel/dp_mem_chunks.c
 * used to do.
 *
 * Sortable columns: 's' size, 'c' count, 'w' waste, 't' waste %. The
 * sort runs in userspace via qsort.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <tilck/common/syscalls.h>
#include <tilck/common/dp_abi.h>

#include "term.h"
#include "tui_layout.h"
#include "dp_int.h"
#include "dp_panel.h"

#define KB_   1024ULL
#define MB_   (1024ULL * 1024ULL)

#define MAX_CHUNKS  4096

static struct dp_kmalloc_chunk chunks[MAX_CHUNKS];
static int chunks_count;
static int got_data;
static int load_errno;
static char order_by = 's';
static int row;

static long dp_cmd_get_chunks(struct dp_kmalloc_chunk *buf, unsigned long max)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_GET_KMALLOC_CHUNKS,
                  (long)buf, (long)max, 0L, 0L);
}

static int cmp_size(const void *a, const void *b)
{
   const struct dp_kmalloc_chunk *x = a;
   const struct dp_kmalloc_chunk *y = b;

   if (x->size > y->size) return -1;
   if (x->size < y->size) return  1;
   return 0;
}

static int cmp_count(const void *a, const void *b)
{
   const struct dp_kmalloc_chunk *x = a;
   const struct dp_kmalloc_chunk *y = b;

   if (x->count > y->count) return -1;
   if (x->count < y->count) return  1;
   return 0;
}

static int cmp_waste(const void *a, const void *b)
{
   const struct dp_kmalloc_chunk *x = a;
   const struct dp_kmalloc_chunk *y = b;

   if (x->max_waste > y->max_waste) return -1;
   if (x->max_waste < y->max_waste) return  1;
   return 0;
}

static int cmp_waste_p(const void *a, const void *b)
{
   const struct dp_kmalloc_chunk *x = a;
   const struct dp_kmalloc_chunk *y = b;

   if (x->max_waste_p > y->max_waste_p) return -1;
   if (x->max_waste_p < y->max_waste_p) return  1;
   return 0;
}

static void resort(void)
{
   int (*cmp)(const void *, const void *) = cmp_size;

   switch (order_by) {
      case 'c': cmp = cmp_count;   break;
      case 'w': cmp = cmp_waste;   break;
      case 't': cmp = cmp_waste_p; break;
      case 's':
      default:  cmp = cmp_size;    break;
   }

   qsort(chunks, (size_t)chunks_count, sizeof(chunks[0]), cmp);
}

static void dp_chunks_on_enter(void)
{
   long rc = dp_cmd_get_chunks(chunks, MAX_CHUNKS);

   if (rc < 0) {
      got_data = 0;
      load_errno = errno;
      chunks_count = 0;
      return;
   }

   got_data = 1;
   chunks_count = (int)rc;
   resort();
}

static enum dp_kb_handler_action
dp_chunks_keypress(struct key_event ke)
{
   if (!ke.print_char)
      return dp_kb_handler_nak;

   switch (ke.print_char) {
      case 's':
      case 'c':
      case 'w':
      case 't':
         order_by = ke.print_char;
         resort();
         ui_need_update = true;
         return dp_kb_handler_ok_and_continue;
   }

   return dp_kb_handler_nak;
}

static void dp_show_chunks(void)
{
   row = tui_screen_start_row;

   if (!got_data) {

      if (load_errno == EOPNOTSUPP || load_errno == ENOTSUP) {

         dp_writeln(
            "Not available: recompile with KRN_KMALLOC_HEAVY_STATS=1");

      } else {

         dp_writeln(E_COLOR_BR_RED
                    "TILCK_CMD_DP_GET_KMALLOC_CHUNKS failed (errno=%d)"
                    RESET_ATTRS, load_errno);
      }

      return;
   }

   /* Aggregate sums of allocated and waste */
   unsigned long long lf_allocs = 0;
   unsigned long long lf_waste  = 0;

   for (int i = 0; i < chunks_count; i++) {

      lf_allocs += (unsigned long long)chunks[i].size *
                   (unsigned long long)chunks[i].count;
      lf_waste  += chunks[i].max_waste;
   }

   const unsigned long long lf_tot = lf_allocs + lf_waste;

   dp_writeln("Chunk sizes count:         %5d sizes", chunks_count);
   dp_writeln("Lifetime data allocated:   %5llu %s [actual: %llu %s]",
              lf_allocs < 32 * MB_ ? lf_allocs / KB_ : lf_allocs / MB_,
              lf_allocs < 32 * MB_ ? "KB" : "MB",
              lf_tot   < 32 * MB_ ? lf_tot / KB_   : lf_tot / MB_,
              lf_tot   < 32 * MB_ ? "KB" : "MB");

   dp_writeln("Lifetime max data waste:   %5llu %s (%llu.%llu%%)",
              lf_waste < 32 * MB_ ? lf_waste / KB_ : lf_waste / MB_,
              lf_waste < 32 * MB_ ? "KB" : "MB",
              lf_tot ? lf_waste * 100 / lf_tot : 0,
              lf_tot ? (lf_waste * 1000 / lf_tot) % 10 : 0);

   dp_writeln(
      "Order by: "
      E_COLOR_BR_WHITE "s" RESET_ATTRS "ize, "
      E_COLOR_BR_WHITE "c" RESET_ATTRS "ount, "
      E_COLOR_BR_WHITE "w" RESET_ATTRS "aste, "
      "was" E_COLOR_BR_WHITE "t" RESET_ATTRS "e (%%)");

   dp_writeln(" ");

   dp_writeln(
                 "%s" "   Size   "      RESET_ATTRS
      TERM_VLINE "%s" "  Count  "       RESET_ATTRS
      TERM_VLINE "%s" " Max waste "     RESET_ATTRS
      TERM_VLINE "%s" " Max waste (%%)" RESET_ATTRS,
      order_by == 's' ? E_COLOR_BR_WHITE REVERSE_VIDEO : "",
      order_by == 'c' ? E_COLOR_BR_WHITE REVERSE_VIDEO : "",
      order_by == 'w' ? E_COLOR_BR_WHITE REVERSE_VIDEO : "",
      order_by == 't' ? E_COLOR_BR_WHITE REVERSE_VIDEO : "");

   dp_writeln(
      GFX_ON
      "qqqqqqqqqqnqqqqqqqqqnqqqqqqqqqqqnqqqqqqqqqqqqqqqqqq"
      GFX_OFF
   );

   for (int i = 0; i < chunks_count; i++) {

      const unsigned long long waste = chunks[i].max_waste;

      dp_writeln("%9llu "
                 TERM_VLINE " %7llu "
                 TERM_VLINE " %6llu %s "
                 TERM_VLINE " %6u.%u%%",
                 (unsigned long long)chunks[i].size,
                 (unsigned long long)chunks[i].count,
                 waste < KB_ ? waste : waste / KB_,
                 waste < KB_ ? "B " : "KB",
                 chunks[i].max_waste_p / 10,
                 chunks[i].max_waste_p % 10);
   }

   dp_writeln(" ");
}

static struct dp_screen dp_chunks_screen = {
   .index = 6,
   .label = "MemChunks",
   .draw_func = dp_show_chunks,
   .on_dp_enter = dp_chunks_on_enter,
   .on_keypress_func = dp_chunks_keypress,
};

__attribute__((constructor))
static void dp_chunks_register(void)
{
   dp_register_screen(&dp_chunks_screen);
}
