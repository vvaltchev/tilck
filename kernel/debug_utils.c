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

#include <elf.h>         // system header
#include <multiboot.h>   // system header in include/system_headers

/* --------- Color constants ---------- */
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[93m"
#define RESET_ATTRS   "\033[0m"
/* ------------------------------------ */

volatile bool __in_panic;

#define SHOW_INT(name, val)   printk(NO_PREFIX "%-35s: %d\n", name, val)
#define DUMP_STR_OPT(opt)     printk(NO_PREFIX "%-35s: %s\n", #opt, opt)
#define DUMP_INT_OPT(opt)     printk(NO_PREFIX "%-35s: %d\n", #opt, opt)
#define DUMP_BOOL_OPT(opt)    printk(NO_PREFIX "%-35s: %u\n", #opt, opt)

void debug_show_opts(void)
{
   printk(NO_PREFIX "\n");
   printk(NO_PREFIX "------------------- BUILD OPTIONS ------------------\n");
#ifdef RELEASE
   DUMP_INT_OPT(RELEASE);
#endif

   DUMP_STR_OPT(BUILDTYPE_STR);

   // Non-boolean kernel options
   DUMP_INT_OPT(TIMER_HZ);
   DUMP_INT_OPT(USER_STACK_PAGES);

   // Boolean options ENABLED by default
   DUMP_BOOL_OPT(KERNEL_TRACK_NESTED_INTERRUPTS);
   DUMP_BOOL_OPT(PANIC_SHOW_STACKTRACE);
   DUMP_BOOL_OPT(DEBUG_CHECKS_IN_RELEASE_BUILD);
   DUMP_BOOL_OPT(KERNEL_SELFTESTS);

   // Boolean options DISABLED by default
   DUMP_BOOL_OPT(KERNEL_GCOV);
   DUMP_BOOL_OPT(FORK_NO_COW);
   DUMP_BOOL_OPT(MMAP_NO_COW);
   DUMP_BOOL_OPT(PANIC_SHOW_REGS);
   DUMP_BOOL_OPT(KMALLOC_FREE_MEM_POISONING);
   DUMP_BOOL_OPT(KMALLOC_SUPPORT_DEBUG_LOG);
   DUMP_BOOL_OPT(KMALLOC_SUPPORT_LEAK_DETECTOR);
   DUMP_BOOL_OPT(KMALLOC_HEAPS_CREATION_DEBUG);
   DUMP_BOOL_OPT(BOOTLOADER_POISON_MEMORY);
   printk(NO_PREFIX "\n");

#ifndef UNIT_TEST_ENVIRONMENT

   printk(NO_PREFIX "------------------- RUNTIME ------------------\n");
   SHOW_INT("TERM_ROWS", term_get_rows(get_curr_term()));
   SHOW_INT("TERM_COLS", term_get_cols(get_curr_term()));
   SHOW_INT("USE_FRAMEBUFFER", use_framebuffer());
   SHOW_INT("FB_OPT_FUNCS", fb_is_using_opt_funcs());
   SHOW_INT("FB_RES_X", fb_get_res_x());
   SHOW_INT("FB_RES_Y", fb_get_res_y());
   SHOW_INT("FB_BBP", fb_get_bbp());
   SHOW_INT("FB_FONT_W", fb_get_font_w());
   SHOW_INT("FB_FONT_H", fb_get_font_h());
   printk(NO_PREFIX "\n");

#endif
}

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

void debug_show_spurious_irq_count(void)
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

void debug_show_task_list(void)
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

static int debug_f_key_press_handler(u32 key, u8 c)
{
   if (!kb_is_ctrl_pressed())
      return KB_HANDLER_NAK;

   switch (key) {

      case KEY_F1:
         debug_show_opts();
         return KB_HANDLER_OK_AND_STOP;

      case KEY_F2:
         debug_kmalloc_dump_mem_usage();
         return KB_HANDLER_OK_AND_STOP;

      case KEY_F3:
         dump_system_memory_map();
         return KB_HANDLER_OK_AND_STOP;

      case KEY_F4:
         debug_show_task_list();
         return KB_HANDLER_OK_AND_STOP;

      case KEY_F5:
         debug_show_spurious_irq_count();
         return KB_HANDLER_OK_AND_STOP;

      default:
         return KB_HANDLER_NAK;
   }
}

static keypress_handler_elem debug_keypress_handler_elem =
{
   .handler = &debug_f_key_press_handler
};

void register_debug_kernel_keypress_handler(void)
{
   kb_register_keypress_handler(&debug_keypress_handler_elem);
}

#endif // UNIT_TEST_ENVIRONMENT

/*
 * NOTE: this flag affect affect sched_alive_thread() only when it is actually
 * running. By default it does *not* run. It gets activated only by the kernel
 * cmdline option -sat.
 */
static volatile bool sched_alive_thread_enabled = true;

static void sched_alive_thread()
{
   for (int counter = 0; ; counter++) {
      if (sched_alive_thread_enabled)
         printk("---- Sched alive thread: %d ----\n", counter);
      kernel_sleep(TIMER_HZ);
   }
}

void init_extra_debug_features()
{
   if (kopt_sched_alive_thread)
      if (kthread_create(&sched_alive_thread, NULL) < 0)
         panic("Unable to create a kthread for sched_alive_thread()");
}

void set_sched_alive_thread_enabled(bool enabled)
{
   sched_alive_thread_enabled = enabled;
}


#if SLOW_DEBUG_REF_COUNT

/*
 * Set here the address of the ref_count to track.
 */
void *debug_refcount_obj = (void *)0;

/* Return the new value */
int __retain_obj(int *ref_count)
{
   int ret;
   ATOMIC(int) *atomic = (ATOMIC(int) *)ref_count;
   ret = atomic_fetch_add_explicit(atomic, 1, mo_relaxed) + 1;

   if (!debug_refcount_obj || ref_count == debug_refcount_obj) {
      printk(COLOR_GREEN "refcount at %p: %d -> %d" RESET_ATTRS "\n",
             ref_count, ret-1, ret);
   }

   return ret;
}

/* Return the new value */
int __release_obj(int *ref_count)
{
   int old, ret;
   ATOMIC(int) *atomic = (ATOMIC(int) *)ref_count;
   old = atomic_fetch_sub_explicit(atomic, 1, mo_relaxed);
   ASSERT(old > 0);
   ret = old - 1;

   if (!debug_refcount_obj || ref_count == debug_refcount_obj) {
      printk(COLOR_RED "refcount at %p: %d -> %d" RESET_ATTRS "\n",
             ref_count, old, ret);
   }

   return ret;
}
#endif
