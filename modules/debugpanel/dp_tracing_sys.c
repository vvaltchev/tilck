/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_tracing.h>

#include <tilck/common/basic_defs.h>
#include <tilck/mods/tracing.h>

#include "termutil.h"
#include "dp_tracing_int.h"

#if MOD_tracing

static inline bool
dp_should_full_dump_param(bool exp_block,
                          enum sys_param_kind kind,
                          enum trace_event_type t)
{
   return kind == sys_param_in_out ||
          (t == te_sys_enter && kind == sys_param_in) ||
          (t == te_sys_exit && (!exp_block || kind == sys_param_out));
}

static const char *
dp_get_esc_color_for_param(const struct sys_param_type *t, const char *rb)
{
   if (rb[0] == '\"' && t->ui_type == ui_type_string)
      return E_COLOR_RED;

   if (t == &ptype_errno_or_val && rb[0] == '-')
      return E_COLOR_WHITE_ON_RED;

   if (t->ui_type == ui_type_integer)
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
                                 struct trace_event *event,
                                 const struct syscall_info *si,
                                 const struct sys_param_info *p,
                                 const struct sys_param_type *type)
{
   char *data;
   size_t data_size;
   long hlp = -1; /* helper param, means "real_size" for ptype_buffer */
   struct syscall_event_data *e = &event->sys_ev;

   if (p->helper_param_name) {

      int idx = tracing_get_param_idx(si, p->helper_param_name);
      ASSERT(idx >= 0);

      hlp = (long) e->args[idx];
   }

   if (!tracing_get_slot(event, si, i, &data, &data_size)) {

      ASSERT(type->dump_from_val);

      if (!type->dump_from_val(e->args[i], hlp, rend_bufs[i], REND_BUF_SZ))
         snprintk(rend_bufs[i], REND_BUF_SZ, "(raw) %p", e->args[i]);

   } else {

      long sz = -1;
      ASSERT(type->dump);

      if (p->helper_param_name)
         sz = hlp;

      sz = MIN(sz, (long)data_size);

      if (p->real_sz_in_ret && event->type == te_sys_exit)
         hlp = e->retval >= 0 ? e->retval : 0;

      if (!type->dump(e->args[i], data, sz, hlp, rend_bufs[i], REND_BUF_SZ))
         snprintk(rend_bufs[i], REND_BUF_SZ, "(raw) %p", e->args[i]);
   }
}

static void
dp_render_minimal_dump_single_param(int i, struct trace_event *event)
{
   struct syscall_event_data *e = &event->sys_ev;

   if (!ptype_voidp.dump_from_val(e->args[i], -1, rend_bufs[i], REND_BUF_SZ))
      panic("Unable to serialize a ptype_voidp in a render buf");
}

static void
dp_dump_syscall_with_info(struct trace_event *e,
                          const char *sys_name,
                          const struct syscall_info *si)
{
   used_rend_bufs = 0;

   for (int i = 0; i < si->n_params; i++) {

      bzero(rend_bufs[i], REND_BUF_SZ);

      const struct sys_param_info *p = &si->params[i];
      const struct sys_param_type *type = p->type;

      if (p->invisible)
         continue;

      if (dp_should_full_dump_param(exp_block(si), p->kind, e->type)) {

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
dp_dump_ret_val(const struct syscall_info *si, long retval)
{
   if (!si) {

      if (retval <= 1024 * 1024) {

         if (retval >= 0) {

            /* we guess it's just a number */
            dp_write_raw(E_COLOR_BR_BLUE "%d" RESET_ATTRS, retval);

         } else {

            /* we guess it's an errno */
            dp_write_raw(E_COLOR_WHITE_ON_RED "-%s" RESET_ATTRS,
                         get_errno_name(-retval));
         }

      } else {

         /* we guess it's a pointer */
         dp_write_raw("%p", retval);
      }

      return;
   }

   const struct sys_param_type *rt = si->ret_type;
   ASSERT(rt->dump_from_val);

   if (!rt->dump_from_val((ulong)retval, -1, rend_bufs[0], REND_BUF_SZ)) {
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
dp_dump_syscall_event(struct trace_event *event,
                      const char *sys_name,
                      const struct syscall_info *si)
{
   struct syscall_event_data *e = &event->sys_ev;

   if (event->type == te_sys_enter) {

      dp_write_raw(E_COLOR_BR_GREEN "ENTER" RESET_ATTRS " ");

   } else {

      if (!si || exp_block(si))
         dp_write_raw(E_COLOR_BR_BLUE "EXIT" RESET_ATTRS " ");
      else
         dp_write_raw(E_COLOR_YELLOW "CALL" RESET_ATTRS " ");
   }

   if (si)
      dp_dump_syscall_with_info(event, sys_name, si);
   else
      dp_write_raw("%s()", sys_name);

   if (event->type == te_sys_exit) {

      dp_write_raw(" -> ");
      dp_dump_ret_val(si, e->retval);
   }

   dp_write_raw("\n");
}

void
dp_handle_syscall_event(struct trace_event *e)
{
   const char *sys_name = NULL;
   const struct syscall_info *si = NULL;
   struct syscall_event_data *se = &e->sys_ev;

   sys_name = tracing_get_syscall_name(se->sys);
   ASSERT(sys_name);
   sys_name += 4; /* skip the "sys_" prefix */
   si = tracing_get_syscall_info(se->sys);
   dp_dump_syscall_event(e, sys_name, si);
}

#endif // #if MOD_tracing
