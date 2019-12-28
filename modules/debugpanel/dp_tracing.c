/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/datetime.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>

#include <tilck/mods/tracing.h>

#include "termutil.h"
#include "tracing_int.h"

#define REND_BUF_SIZE                              128
static char *rend_bufs[6];
static int used_rend_bufs;

void init_dp_tracing(void)
{
   for (int i = 0; i < 6; i++) {

      if (!(rend_bufs[i] = kmalloc(REND_BUF_SIZE)))
         panic("[dp] Unable to allocate rend_buf[%d]", i);
   }
}

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

static bool
tracing_ui_wait_for_enter(void)
{
   int rc;
   char c;

   while (true) {

      kernel_sleep(TIMER_HZ / 10);
      rc = vfs_read(dp_input_handle, &c, 1);

      if (rc == -EAGAIN)
         continue;

      if (rc != 1)
         return false; /* error */

      if (c == DP_KEY_ENTER)
         return true;

      if (c == DP_KEY_CTRL_C)
         return false;
   }
}

static inline bool
dp_should_full_dump_param(enum sys_param_kind kind, enum trace_event_type t)
{
   return kind == sys_param_in_out ||
          (t == te_sys_enter && kind == sys_param_in) ||
          (t == te_sys_exit && kind == sys_param_out);
}

static const char *
dp_get_esc_color_for_param(const struct sys_param_type *t, const char *rb)
{
   if (rb[0] == '\"' && (t == &ptype_buffer || t == &ptype_path))
      return E_COLOR_RED;

   if (t == &ptype_int)
      return E_COLOR_BR_BLUE;

   return "";
}

static void
dp_dump_rendered_params(const char *sys_name, const struct syscall_info *si)
{
   int dumped_bufs = 0;

   dp_write_raw("%s(", sys_name);

   for (int i = 0; i < si->n_params; i++) {

      const struct sys_param_info *p = &si->params[i];

      if (!rend_bufs[i][0])
         continue;

      dp_write_raw(E_COLOR_MAGENTA "%s" RESET_ATTRS ": ", p->name);

      dp_write_raw(
         "%s%s" RESET_ATTRS,
         dp_get_esc_color_for_param(p->type, rend_bufs[i]),
         rend_bufs[i]
      );

      if (dumped_bufs < used_rend_bufs - 1)
         dp_write_raw(", ");

      dumped_bufs++;
   }

   dp_write_raw(")");
}

static void
dp_render_full_dump_single_param(int i,
                                 struct trace_event *e,
                                 const struct syscall_info *si,
                                 const struct sys_param_info *p,
                                 const struct sys_param_type *type)
{
   char *data;
   size_t data_size;
   sptr real_sz = -1;

   if (!tracing_get_slot(e, si, i, &data, &data_size)) {

      ASSERT(type->dump_from_val);

      if (!type->dump_from_val(e->args[i], rend_bufs[i], REND_BUF_SIZE))
         snprintk(rend_bufs[i], REND_BUF_SIZE, "(raw) %p", e->args[i]);

   } else {

      sptr sz = -1;
      ASSERT(type->dump_from_data);

      if (p->size_param_name) {

         int idx = tracing_get_param_idx(si, p->size_param_name);
         ASSERT(idx >= 0);

         sz = (sptr) e->args[idx];
         real_sz = sz;
      }

      sz = MIN(sz, (sptr)data_size);

      if (p->real_sz_in_ret && e->type == te_sys_exit)
         real_sz = e->retval;

      if (!type->dump_from_data(data, sz, real_sz, rend_bufs[i], REND_BUF_SIZE))
         snprintk(rend_bufs[i], REND_BUF_SIZE, "(raw) %p", e->args[i]);
   }
}

static void
dp_render_minimal_dump_single_param(int i, struct trace_event *e)
{
   if (!ptype_voidp.dump_from_val(e->args[i], rend_bufs[i], REND_BUF_SIZE))
      panic("Unable to serialize a ptype_voidp in a render buf");
}

static void
dp_dump_syscall_with_info(struct trace_event *e,
                          const char *sys_name,
                          const struct syscall_info *si)
{
   used_rend_bufs = 0;

   for (int i = 0; i < si->n_params; i++) {

      bzero(rend_bufs[i], REND_BUF_SIZE);

      const struct sys_param_info *p = &si->params[i];
      const struct sys_param_type *type = p->type;

      if (dp_should_full_dump_param(p->kind, e->type)) {

         dp_render_full_dump_single_param(i, e, si, p, type);
         used_rend_bufs++;

      } else if (e->type == te_sys_enter) {

         dp_render_minimal_dump_single_param(i, e);
         used_rend_bufs++;
      }
   }

   dp_dump_rendered_params(sys_name, si);
}

static void
dp_dump_ret_val(const struct syscall_info *si, sptr retval)
{
   if (!si) {

      if (retval <= 1024 * 1024) {

         /* we guess it's just a number or an errno */
         dp_write_raw(E_COLOR_BR_BLUE "%d" RESET_ATTRS, retval);

      } else {

         /* we guess it's a pointer */
         dp_write_raw("%p", retval);
      }

      return;
   }

   const struct sys_param_type *rt = si->ret_type;
   ASSERT(rt->dump_from_val);

   if (!rt->dump_from_val((uptr)retval, rend_bufs[0], REND_BUF_SIZE)) {
      dp_write_raw("(raw) %p", retval);
      return;
   }

   dp_write_raw(
      "%s%s" RESET_ATTRS,
      dp_get_esc_color_for_param(si->ret_type, rend_bufs[0]),
      rend_bufs[0]
   );
}

static void
dp_dump_syscall_event(struct trace_event *e,
                      const char *sys_name,
                      const struct syscall_info *si)
{
   if (e->type == te_sys_enter)
      dp_write_raw(E_COLOR_BR_GREEN "ENTER" RESET_ATTRS " ");
   else
      dp_write_raw(E_COLOR_BR_BLUE "EXIT" RESET_ATTRS " ");

   if (si)
      dp_dump_syscall_with_info(e, sys_name, si);
   else
      dp_write_raw("%s()", sys_name);

   if (e->type == te_sys_exit) {

      dp_write_raw(" -> ");
      dp_dump_ret_val(si, e->retval);
   }

   dp_write_raw("\r\n");
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

   if (!tracing_ui_wait_for_enter())
      goto out;

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

out:
   ui_need_update = true;
   dp_set_cursor_enabled(false);
   return kb_handler_ok_and_continue;
}
