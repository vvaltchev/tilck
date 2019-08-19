/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/fb_console.h>
#include <tilck/kernel/cmdline.h>

#include "termutil.h"

static void debug_dump_slow_irq_handler_count(void)
{
   extern u32 slow_timer_irq_handler_count;

   if (KERNEL_TRACK_NESTED_INTERRUPTS) {
      printk(NO_PREFIX "   Slow timer irq handler counter: %u\n",
             slow_timer_irq_handler_count);
   }
}

static void debug_dump_spur_irq_count(void)
{
   extern u32 spur_irq_count;
   const u64 ticks = get_ticks();

   if (ticks > TIMER_HZ)
      printk(NO_PREFIX "   Spurious IRQ count: %u (%u/sec)\n",
             spur_irq_count,
             spur_irq_count / (ticks / TIMER_HZ));
   else
      printk(NO_PREFIX "   Spurious IRQ count: %u (< 1 sec)\n",
             spur_irq_count, spur_irq_count);
}

static void debug_dump_unhandled_irq_count(void)
{
   extern u32 unhandled_irq_count[256];
   u32 tot_count = 0;

   for (u32 i = 0; i < ARRAY_SIZE(unhandled_irq_count); i++)
      tot_count += unhandled_irq_count[i];

   if (!tot_count)
      return;

   printk(NO_PREFIX "\n");
   printk(NO_PREFIX "Unhandled IRQs count table\n\n");

   for (u32 i = 0; i < ARRAY_SIZE(unhandled_irq_count); i++) {

      if (unhandled_irq_count[i])
         printk(NO_PREFIX "   IRQ #%3u: %3u unhandled\n", i,
                unhandled_irq_count[i]);
   }

   printk(NO_PREFIX "\n");
}

static void debug_show_spurious_irq_count(void)
{
   printk(NO_PREFIX "\n");
   printk(NO_PREFIX "Kernel IRQ-related counters\n\n");

   debug_dump_slow_irq_handler_count();
   debug_dump_spur_irq_count();
   debug_dump_unhandled_irq_count();
}


static int debug_get_tn_for_tasklet_runner(task_info *ti)
{
   for (u32 i = 0; i < MAX_TASKLET_THREADS; i++)
      if (get_tasklet_runner(i) == ti)
         return (int)i;

   return -1;
}

enum task_dump_util_str {

   HEADER,
   ROW_FMT,
   HLINE
};

static const char *
debug_get_task_dump_util_str(enum task_dump_util_str t)
{
   static bool initialized;
   static char fmt[80] = NO_PREFIX;
   static char hfmt[80];
   static char header[256];
   static char hline_sep[256] =
      "+-----------+-------+-------+----------+-----+";

   static char *hline_sep_end = &hline_sep[sizeof(hline_sep)];

   if (!initialized) {

      int path_field_len = (term_get_cols(get_curr_term()) - 80) + 31;

      snprintk(fmt + 4, sizeof(fmt) - 4,
               "| %%-9d | %%-5d | %%-5d | %%-8s | %%-3d | %%-%ds |\n",
               path_field_len);

      snprintk(hfmt, sizeof(hfmt),
               "| %%-9s | %%-5s | %%-5s | %%-8s | %%-3s | %%-%ds |\n",
               path_field_len);

      snprintk(header, sizeof(header), hfmt,
               "tid", "pid", "ppid", "state", "tty", "path or kernel thread");

      char *p = hline_sep + strlen(hline_sep);

      for (int i = 0; i < path_field_len + 2 && p < hline_sep_end; i++, p++) {
         *p = '-';
      }

      if (p < hline_sep_end)
         *p++ = '+';

      if (p < hline_sep_end)
         *p++ = '\n';

      initialized = true;
   }

   switch (t) {
      case HEADER:
         return header;

      case ROW_FMT:
         return fmt;

      case HLINE:
         return hline_sep;

      default:
         NOT_REACHED();
   }
}

static int debug_per_task_cb(void *obj, void *arg)
{
   const char *fmt = debug_get_task_dump_util_str(ROW_FMT);
   task_info *ti = obj;

   if (!ti->tid)
      return 0; /* skip the main kernel task */

   const char *state = debug_get_state_name(ti->state);
   int ttynum = tty_get_num(ti->pi->proc_tty);

   if (!is_kernel_thread(ti)) {
      printk(fmt, ti->tid, ti->pi->pid,
             ti->pi->parent_pid, state, ttynum, ti->pi->filepath);
      return 0;
   }

   char buf[128];
   const char *kfunc = find_sym_at_addr((uptr)ti->what, NULL, NULL);

   if (!is_tasklet_runner(ti)) {
      snprintk(buf, sizeof(buf), "<ker: %s>", kfunc);
   } else {
      snprintk(buf, sizeof(buf), "<ker: %s[%d]>",
               kfunc, debug_get_tn_for_tasklet_runner(ti));
   }

   printk(fmt, ti->tid, ti->pi->pid, ti->pi->parent_pid, state, 0, buf);
   return 0;
}

static void debug_dump_task_table_hr(void)
{
   printk(NO_PREFIX "%s", debug_get_task_dump_util_str(HLINE));
}

static void debug_show_task_list(void)
{
   printk(NO_PREFIX "\n\n");

   debug_dump_task_table_hr();
   printk(NO_PREFIX "%s", debug_get_task_dump_util_str(HEADER));

   debug_dump_task_table_hr();

   disable_preemption();
   {
      iterate_over_tasks(debug_per_task_cb, NULL);
   }
   enable_preemption();

   debug_dump_task_table_hr();
   printk(NO_PREFIX "\n");
}

#ifndef UNIT_TEST_ENVIRONMENT


void dp_func1_options(void);

static void func2_memmap(void)
{
   dp_move_cursor(dp_rows / 2, 0);
   printk(NO_PREFIX "func2: memmap");
}

static void func3_heaps(void)
{
   dp_move_cursor(dp_rows / 2, 0);
   printk(NO_PREFIX "func3: heaps");
}

static void func4_tasks(void)
{
   dp_move_cursor(dp_rows / 2, 0);
   printk(NO_PREFIX "func4: tasks");
}

static void func5_irqs(void)
{
   dp_move_cursor(dp_rows / 2, 0);
   printk(NO_PREFIX "func5: irqs");
}

static bool in_debug_panel;
static tty *dp_tty;
static tty *saved_tty;
int dp_rows;
int dp_cols;
static void (*dp_func)(void) = dp_func1_options;

static int dp_debug_panel_off_keypress(u32 key, u8 c)
{
   if (kb_is_ctrl_pressed() && key == KEY_F12) {

      if (!dp_tty) {

         dp_tty = create_tty_nodev();

         if (!dp_tty) {
            printk("ERROR: no enough memory for debug panel's TTY\n");
            return KB_HANDLER_OK_AND_STOP;
         }
      }

      saved_tty = get_curr_tty();

      if (set_curr_tty(dp_tty) == 0) {
         in_debug_panel = true;
         dp_rows = term_get_rows(get_curr_term());
         dp_cols = term_get_cols(get_curr_term());
      }

      return KB_HANDLER_OK_AND_STOP;
   }

   return KB_HANDLER_NAK;
}


static int dp_keypress_handler(u32 key, u8 c)
{
   if (kopt_serial_console)
      return KB_HANDLER_NAK;

   if (!in_debug_panel) {
      if (dp_debug_panel_off_keypress(key, c) == KB_HANDLER_NAK)
         return KB_HANDLER_NAK;
   }

   if (!kb_is_ctrl_pressed() && key == KEY_F12) {

      if (set_curr_tty(saved_tty) == 0) {
         in_debug_panel = false;
      }

      return KB_HANDLER_OK_AND_STOP;
   }

   switch (key) {

      case KEY_F1:
         dp_func = dp_func1_options;
         break;

      case KEY_F2:
         dp_func = func2_memmap;
         break;

      case KEY_F3:
         dp_func = func3_heaps;
         break;

      case KEY_F4:
         dp_func = func4_tasks;
         break;

      case KEY_F5:
         dp_func = func5_irqs;
         break;
   }

   dp_clear();
   dp_move_cursor(0, 0);
   printk(NO_PREFIX);
   printk(NO_PREFIX COLOR_YELLOW "[TilckDebugPanel] " RESET_ATTRS);

   dp_write_header(1, "[Options] ", dp_func == dp_func1_options);
   dp_write_header(2, "[MemMap] ", dp_func == func2_memmap);
   dp_write_header(3, "[Heaps] ", dp_func == func3_heaps);
   dp_write_header(4, "[Tasks] ", dp_func == func4_tasks);
   dp_write_header(5, "[IRQs] ", dp_func == func5_irqs);
   dp_write_header(12, "[Quit] ", false);

   dp_func();
   dp_move_cursor(999,999);

   // switch (key) {

   //    case KEY_F1:
   //       debug_show_opts();
   //       return KB_HANDLER_OK_AND_STOP;

   //    case KEY_F2:
   //       debug_kmalloc_dump_mem_usage();
   //       return KB_HANDLER_OK_AND_STOP;

   //    case KEY_F3:
   //       dump_system_memory_map();
   //       return KB_HANDLER_OK_AND_STOP;

   //    case KEY_F4:
   //       debug_show_task_list();
   //       return KB_HANDLER_OK_AND_STOP;

   //    case KEY_F5:
   //       debug_show_spurious_irq_count();
   //       return KB_HANDLER_OK_AND_STOP;
   // }

   return KB_HANDLER_OK_AND_STOP;
}

static keypress_handler_elem debug_keypress_handler_elem =
{
   .handler = &dp_keypress_handler
};

__attribute__((constructor))
static void init_debug_panel()
{
   kb_register_keypress_handler(&debug_keypress_handler_elem);
}

#endif // UNIT_TEST_ENVIRONMENT
