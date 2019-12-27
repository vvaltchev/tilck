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
dp_dump_syscall_with_info(struct trace_event *e,
                          const char *sys_name,
                          const struct syscall_info *si)
{
   int params_printed = 0;
   char buf[128];

   dp_write_raw("%s(", sys_name);

   for (int i = 0; i < si->n_params; i++) {

      const struct sys_param_info *nfo = &si->params[i];
      const struct sys_param_type *type = nfo->type;

      if (nfo->kind == sys_param_in_out ||
          (e->type == te_sys_enter && nfo->kind == sys_param_in) ||
          (e->type == te_sys_exit && nfo->kind == sys_param_out))
      {
         bzero(buf, sizeof(buf));

         dp_write_raw("\r\n  ");
         dp_write_raw(E_COLOR_MAGENTA "%s" RESET_ATTRS, nfo->name);
         dp_write_raw(": ");

         if (nfo->slot == NO_SLOT) {

            ASSERT(type->dump_from_val);

            if (type->dump_from_val(e->args[i], buf, sizeof(buf)))
               dp_write_raw("%s", buf);
            else
               dp_write_raw("<no space in output buf>");

         } else {

            ASSERT(type->dump_from_data);

            char *data;
            size_t data_size;

            tracing_get_slot(e, si, nfo, &data, &data_size);

            if (type->dump_from_data(data, buf, sizeof(buf)))
               dp_write_raw("%s", buf);
            else
               dp_write_raw("<no space in output buf>");
         }

         params_printed++;
      }
   }

   if (params_printed)
      dp_write_raw("\r\n)");
   else
      dp_write_raw(")");
}

static void
dp_dump_syscall_event(struct trace_event *e,
                      const char *sys_name,
                      const struct syscall_info *si)
{
   if (e->type == te_sys_enter)
      dp_write_raw(E_COLOR_GREEN "ENTER" RESET_ATTRS " ");
   else
      dp_write_raw(E_COLOR_BR_RED "EXIT" RESET_ATTRS " ");

   if (si)
      dp_dump_syscall_with_info(e, sys_name, si);
   else
      dp_write_raw("%s()", sys_name);

   if (e->type == te_sys_enter) {
      dp_write_raw("\r\n");
   } else {
      dp_write_raw(" -> %d\r\n", e->retval);
   }
}

static void
dp_dump_tracing_event(struct trace_event *e)
{
   const char *sys_name = NULL;
   const struct syscall_info *si = NULL;

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
      si = tracing_get_syscall_info(e->sys);
      dp_dump_syscall_event(e, sys_name, si);

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
