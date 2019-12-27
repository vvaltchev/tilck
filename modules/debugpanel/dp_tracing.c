/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/datetime.h>
#include <tilck/kernel/sched.h>

#include <tilck/mods/tracing.h>
#include "termutil.h"

static void
tracing_ui_msg1(void)
{
   dp_clear();
   dp_move_cursor(1, 1);
   dp_write_raw(
      E_COLOR_YELLOW
      "Tilck syscall tracing. ENTER: start dumping | Ctrl+C: exit"
      RESET_ATTRS
   );
}

static void
tracing_ui_msg2(void)
{
   dp_clear();
   dp_move_cursor(1, 1);
   dp_write_raw(
      E_COLOR_YELLOW
      "Tilck syscall tracing. Press Ctrl+C to exit"
      RESET_ATTRS
   );
   dp_write_raw("\r\n");
}

static void
tracing_ui_wait_for_enter(void)
{
   int rc;
   char c;

   do {

      rc = vfs_read(dp_input_handle, &c, 1);
      kernel_sleep(TIMER_HZ / 10);

   } while (rc != 1 && c != DP_KEY_ENTER);
}

static void
dp_dump_tracing_event(struct trace_event *e)
{
   const char *sys_name = NULL;

   dp_write_raw(
      "%05u.%03u [%04d] ",
      (u32)(e->sys_time / TS_SCALE),
      (u32)((e->sys_time % TS_SCALE) / (TS_SCALE / 1000)),
      e->tid
   );

   if (e->type == te_sys_enter || e->type == te_sys_exit) {
      sys_name = tracing_get_syscall_name(e->sys);
      ASSERT(sys_name);
      sys_name += 4; /* skip the "sys_" prefix */
   }

   if (e->type == te_sys_enter) {

      dp_write_raw(E_COLOR_GREEN "ENTER" RESET_ATTRS " ");
      dp_write_raw("%s()\r\n", sys_name);

   } else if (e->type == te_sys_exit) {

      dp_write_raw(E_COLOR_BR_RED "EXIT" RESET_ATTRS " ");
      dp_write_raw("%s() -> %d\r\n", sys_name, e->retval);

   } else {

      dp_write_raw(E_COLOR_BR_RED "<unknown event>\r\n" RESET_ATTRS);
   }
}

enum kb_handler_action
dp_tasks_show_trace(void)
{
   struct trace_event e;
   int rc;
   char c;

   dp_set_cursor_enabled(true);

   tracing_ui_msg1();
   tracing_ui_wait_for_enter();
   tracing_ui_msg2();

   while (true) {

      if (!read_trace_event(&e, TIMER_HZ / 10)) {

         /* No tracing event yet, check the input for Ctrl+C */
         rc = vfs_read(dp_input_handle, &c, 1);

         if (rc == 1 && c == DP_KEY_CTRL_C)
            break; /* Ctrl+C hit, exit from here! */

      } else {

         /* We actually read a tracing event, dump it. */
         dp_dump_tracing_event(&e);
      }
   }

   ui_need_update = true;
   dp_set_cursor_enabled(false);
   return kb_handler_ok_and_continue;
}
