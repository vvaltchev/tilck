/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/datetime.h>
#include <tilck/kernel/sched.h>

#include <tilck/mods/tracing.h>

#include "termutil.h"
#include "tracing_int.h"

static char rend_bufs[6][128];

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

static inline bool
dp_should_full_dump_param(enum sys_param_kind kind, enum trace_event_type t)
{
   return kind == sys_param_in_out ||
          (t == te_sys_enter && kind == sys_param_in) ||
          (t == te_sys_exit && kind == sys_param_out);
}

static void
dp_dump_rendered_params(const char *sys_name, const struct syscall_info *si)
{
   dp_write_raw("%s(", sys_name);

   for (int i = 0; i < si->n_params; i++) {

      const struct sys_param_info *nfo = &si->params[i];

      if (!rend_bufs[i][0])
         continue;

      dp_write_raw(E_COLOR_MAGENTA "%s" RESET_ATTRS, nfo->name);
      dp_write_raw(": %s", rend_bufs[i]);

      if (i < si->n_params - 1)
         dp_write_raw(", ");
   }

   dp_write_raw(")");
}

static void
dp_render_full_dump_single_param(int i,
                                 struct trace_event *e,
                                 const struct syscall_info *si,
                                 const struct sys_param_info *nfo,
                                 const struct sys_param_type *type)
{
   char *data;
   size_t data_size;

   if (nfo->slot == NO_SLOT) {

      ASSERT(type->dump_from_val);

      if (!type->dump_from_val(e->args[i], rend_bufs[i], 128))
         memcpy(rend_bufs[i], "<nosp>", 6);

   } else {

      ASSERT(type->dump_from_data);

      tracing_get_slot(e, si, nfo, &data, &data_size);

      if (!type->dump_from_data(data, rend_bufs[i], 128))
         memcpy(rend_bufs[i], "<nosp>", 6);
   }
}

static void
dp_render_minimal_dump_single_param(int i, struct trace_event *e)
{
   if (!ptype_voidp.dump_from_val(e->args[i], rend_bufs[i], 128))
      memcpy(rend_bufs[i], "<nosp>", 6);
}

static void
dp_dump_syscall_with_info(struct trace_event *e,
                          const char *sys_name,
                          const struct syscall_info *si)
{
   bzero(rend_bufs, sizeof(rend_bufs));

   for (int i = 0; i < si->n_params; i++) {

      const struct sys_param_info *nfo = &si->params[i];
      const struct sys_param_type *type = nfo->type;

      if (dp_should_full_dump_param(nfo->kind, e->type))
         dp_render_full_dump_single_param(i, e, si, nfo, type);
      else if (e->type == te_sys_enter)
         dp_render_minimal_dump_single_param(i, e);
   }

   dp_dump_rendered_params(sys_name, si);
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
