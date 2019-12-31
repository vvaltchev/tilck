/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/modules.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/ringbuf.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/bintree.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/debug_utils.h>

#include <tilck/mods/tracing.h>

#define TRACE_BUF_SIZE                       (128 * KB)

struct symbol_node {

   struct bintree_node node;

   u32 sys_n;
   void *vaddr;
   const char *name;
};

static struct kmutex tracing_lock;
static struct kcond tracing_cond;
static struct ringbuf tracing_rb;
static void *tracing_buf;

static u32 syms_count;
static struct symbol_node *syms_buf;
static struct symbol_node *syms_bintree;

static const struct syscall_info **syscalls_info;
static s8 (*params_slots)[MAX_SYSCALLS][6];
static s8 *syscalls_fmts;

static char *traced_syscalls_str;
static int traced_syscalls_count;
bool *traced_syscalls;
bool force_exp_block;

static int
elf_symbol_cb(struct elf_symbol_info *i, void *arg)
{
   struct symbol_node *sym_node;
   int sys_n;

   if (!i->name || strncmp(i->name, "sys_", 4))
      return 0; /* not a syscall symbol */

   if ((sys_n = get_syscall_num(i->vaddr)) < 0)
      return 0; /* not a syscall, just a function start with sys_ */

   sym_node = &syms_buf[syms_count];

   *sym_node = (struct symbol_node) {
      .sys_n = (u32) sys_n,
      .vaddr = i->vaddr,
      .name = i->name,
   };

   bintree_node_init(&sym_node->node);

   bintree_insert_ptr(&syms_bintree,
                      sym_node,
                      struct symbol_node,
                      node,
                      vaddr);

   syms_count++;
   return 0;
}

const char *
tracing_get_syscall_name(u32 n)
{
   void *ptr;
   struct symbol_node *node;

   if (!(ptr = get_syscall_func_ptr(n)))
      return NULL;

   node = bintree_find_ptr(syms_bintree, ptr, struct symbol_node, node, vaddr);

   if (!node)
      return NULL;

   return node->name;
}

int
tracing_get_param_idx(const struct syscall_info *si, const char *name)
{
   for (int i = 0; i < si->n_params; i++) {

      const struct sys_param_info *p = &si->params[i];

      /*
       * NOTE: using pointer comparison instead of strcmp() for strings.
       * This code assumes that in the metadata the same string literal will
       * be used both for the parameter name and the `size_param_name` field.
       */
      if (p->name == name)
         return i;
   }

   return -1;
}

static void trace_syscall_enter_save_params(const struct syscall_info *si,
                                            struct trace_event *e)
{
   char *buf = NULL;
   size_t bs = 0;
   int idx;

   if (!si)
      return;

   for (int i = 0; i < si->n_params; i++) {

      const struct sys_param_info *p = &si->params[i];
      const struct sys_param_type *t = p->type;

      if (t->save && (p->kind == sys_param_in || p->kind == sys_param_in_out))
      {
         sptr sz = -1;

         if (p->size_param_name) {

            idx = tracing_get_param_idx(si, p->size_param_name);
            ASSERT(idx >= 0);

            sz = (sptr) e->args[idx];
         }

         tracing_get_slot(e, si, i, &buf, &bs);
         ASSERT(buf && bs > 0);

         t->save(TO_PTR(e->args[i]), sz, buf, bs);
      }
   }
}

static void
trace_syscall_exit_save_params(const struct syscall_info *si,
                               struct trace_event *e)
{
   char *buf = NULL;
   size_t bs = 0;
   int idx;

   if (!si)
      return;

   for (int i = 0; i < si->n_params; i++) {

      const struct sys_param_info *p = &si->params[i];
      const struct sys_param_type *t = p->type;
      const bool outp = p->kind == sys_param_out || p->kind == sys_param_in_out;

      if (t->save && (!exp_block(si) || outp))
      {
         sptr sz = -1;

         if (p->size_param_name) {

            idx = tracing_get_param_idx(si, p->size_param_name);
            ASSERT(idx >= 0);

            sz = (sptr) e->args[idx];
         }

         tracing_get_slot(e, si, i, &buf, &bs);
         ASSERT(buf && bs > 0);

         t->save(TO_PTR(e->args[i]), sz, buf, bs);
      }
   }
}

void
trace_syscall_enter(u32 sys,
                    uptr a1, uptr a2, uptr a3, uptr a4, uptr a5, uptr a6)
{
   const struct syscall_info *si = tracing_get_syscall_info(sys);

   if (si && !exp_block(si))
      return; /* don't trace the enter event */

   struct trace_event e = {
      .type = te_sys_enter,
      .tid = get_curr_tid(),
      .sys_time = get_sys_time(),
      .sys = sys,
      .args = {a1,a2,a3,a4,a5,a6}
   };

   trace_syscall_enter_save_params(si, &e);

   kmutex_lock(&tracing_lock);
   {
      ringbuf_write_elem(&tracing_rb, &e);
      kcond_signal_one(&tracing_cond);
   }
   kmutex_unlock(&tracing_lock);
}

void
trace_syscall_exit(u32 sys, sptr retval,
                   uptr a1, uptr a2, uptr a3, uptr a4, uptr a5, uptr a6)
{
   const struct syscall_info *si = tracing_get_syscall_info(sys);

   struct trace_event e = {
      .type = te_sys_exit,
      .tid = get_curr_tid(),
      .sys_time = get_sys_time(),
      .sys = sys,
      .retval = retval,
      .args = {a1,a2,a3,a4,a5,a6}
   };

   trace_syscall_exit_save_params(si, &e);

   kmutex_lock(&tracing_lock);
   {
      ringbuf_write_elem(&tracing_rb, &e);
      kcond_signal_one(&tracing_cond);
   }
   kmutex_unlock(&tracing_lock);
}

bool read_trace_event(struct trace_event *e, u32 timeout_ticks)
{
   bool ret;
   kmutex_lock(&tracing_lock);
   {
      if (ringbuf_is_empty(&tracing_rb))
         kcond_wait(&tracing_cond, &tracing_lock, timeout_ticks);

      ret = ringbuf_read_elem(&tracing_rb, e);
   }
   kmutex_unlock(&tracing_lock);
   return ret;
}

const struct syscall_info *
tracing_get_syscall_info(u32 n)
{
   if (n >= MAX_SYSCALLS)
      return NULL;

   return syscalls_info[n];
}

#define NULL_TRACE_EVENT                           ((struct trace_event *)0)

#define GET_SLOT(e, fmt_n, slot_n)               ((e)->fmt##fmt_n.d##slot_n)

#define GET_SLOT_ABS_OFF(fmt_n, slot_n)                                    \
   ((uptr)GET_SLOT(NULL_TRACE_EVENT, fmt_n, slot_n))

#define GET_SLOT_OFF(fmt_n, slot_n)                                        \
   (GET_SLOT_ABS_OFF(fmt_n, slot_n) - GET_SLOT_ABS_OFF(fmt_n, 0))

#define GET_SLOT_SIZE(fmt_n, slot_n)                                       \
   sizeof(GET_SLOT(NULL_TRACE_EVENT, fmt_n, slot_n))

#define CAN_USE_SLOT(sys, fmt_n, slot_n, size)                             \
   (GET_SLOT_SIZE(fmt_n, slot_n) >= size && is_slot_free(sys, slot_n))

static const size_t fmt_offsets[2][4] = {

   /* fmt 0 */
   {
      GET_SLOT_ABS_OFF(0, 0),
      GET_SLOT_ABS_OFF(0, 1),
      GET_SLOT_ABS_OFF(0, 2),
      GET_SLOT_ABS_OFF(0, 3),
   },

   /* fmt 1 */
   {
      GET_SLOT_ABS_OFF(1, 0),
      GET_SLOT_ABS_OFF(1, 1),
      GET_SLOT_ABS_OFF(1, 2),
      0,
   },
};

static const size_t fmt_sizes[2][4] = {

   /* fmt 0 */
   {
      GET_SLOT_SIZE(0, 0),
      GET_SLOT_SIZE(0, 1),
      GET_SLOT_SIZE(0, 2),
      GET_SLOT_SIZE(0, 3),
   },

   /* fmt 1 */
   {
      GET_SLOT_SIZE(1, 0),
      GET_SLOT_SIZE(1, 1),
      GET_SLOT_SIZE(1, 2),
      0,
   },
};

bool
tracing_get_slot(struct trace_event *e,
                 const struct syscall_info *si,
                 int p_idx,
                 char **buf,
                 size_t *size)
{
   const s8 slot = (*params_slots)[e->sys][p_idx];
   s8 fmt;

   if (slot == NO_SLOT)
      return false;

   fmt = syscalls_fmts[si->sys_n];
   *buf = (char *)e + fmt_offsets[fmt][slot];
   *size = fmt_sizes[fmt][slot];
   return true;
}

static bool
is_slot_free(u32 sys, int slot)
{
   ASSERT(slot >= 0);

   for (int i = 0; i < 6; i++)
      if ((*params_slots)[sys][i] == slot)
         return false;

   return true;
}

static bool
alloc_for_fmt0(u32 sys, int p_idx, size_t size)
{
   s8 slot = NO_SLOT;

   if (!size)
      return true; /* we're fine: no need to allocate anything for this param */

   if (CAN_USE_SLOT(sys, 0, 3, size))
      slot = 3;
   else if (CAN_USE_SLOT(sys, 0, 2, size))
      slot = 2;
   else if (CAN_USE_SLOT(sys, 0, 1, size))
      slot = 1;
   else if (CAN_USE_SLOT(sys, 0, 0, size))
      slot = 0;
   else
      return false; /* we failed: another fmt should be used for this syscall */

   (*params_slots)[sys][p_idx] = slot;
   return true;     /* everything is alright */
}

static bool
alloc_for_fmt1(u32 sys, int p_idx, size_t size)
{
   s8 slot = NO_SLOT;

   if (!size)
      return true; /* we're fine: no need to allocate anything for this param */

   if (CAN_USE_SLOT(sys, 1, 2, size))
      slot = 2;
   else if (CAN_USE_SLOT(sys, 1, 1, size))
      slot = 1;
   else if (CAN_USE_SLOT(sys, 1, 0, size))
      slot = 0;
   else
      return false; /* we failed: another fmt should be used for this syscall */

   (*params_slots)[sys][p_idx] = slot;
   return true;     /* everything is alright */
}

static void
tracing_populate_syscalls_info(void)
{
   const struct syscall_info *si;

   for (si = tracing_metadata; si->sys_n != INVALID_SYSCALL; si++) {
      syscalls_info[si->sys_n] = si;
   }
}

static void
tracing_reset_slot_info(u32 sys)
{
   for (int j = 0; j < 6; j++)
      (*params_slots)[sys][j] = NO_SLOT;
}

static void
tracing_allocate_slots_for_params(void)
{
   const struct syscall_info *si;
   bool failed;

   for (u32 i = 0; i < MAX_SYSCALLS; i++)
      tracing_reset_slot_info(i);

   for (si = tracing_metadata; si->sys_n != INVALID_SYSCALL; si++) {

      const struct sys_param_info *p = si->params;
      const u32 sys_n = si->sys_n;

      failed = false;
      syscalls_fmts[sys_n] = 0; /* fmt 0 */

      for (int i = 0; i < si->n_params; i++)
         if ((failed = !alloc_for_fmt0(sys_n, i, p[i].type->slot_size)))
            break;

      if (!failed)
         continue;

      failed = false;
      syscalls_fmts[sys_n] = 1; /* fmt 1 */
      tracing_reset_slot_info(sys_n);

      for (int i = 0; i < si->n_params; i++)
         if ((failed = !alloc_for_fmt1(sys_n, i, p[i].type->slot_size)))
            break;

      if (!failed)
         continue;

      panic("Unable to alloc param slots for syscall #%u", sys_n);
   }
}

static void
debug_tracing_dump_syscalls_fmt(void)
{
   const struct syscall_info *si;
   s8 fmt;

   for (si = tracing_metadata; si->sys_n != INVALID_SYSCALL; si++) {
      fmt = syscalls_fmts[si->sys_n];
      printk("sys #%u -> fmt %d\n", si->sys_n, fmt);
   }
}

void
get_traced_syscalls_str(char *buf, size_t len)
{
   memcpy(buf, traced_syscalls_str, MIN(len, TRACED_SYSCALLS_STR_LEN));
}

int
get_traced_syscalls_count(void)
{
   return traced_syscalls_count;
}

/*
 * Minimalistic wildcard matching function.
 *
 * It supports only '*' at the end of an expression like:
 *
 *    ab*      matches:  abc, ab, abcdefgh, etc.
 *      *      matches EVERYTHING
 *
 * And the jolly character '?' which matches exactly one character (any).
 *
 * NOTE:
 *    ab*c     matches NOTHING because '*' must be the last char, if present.
 *
 */
static bool
simple_wildcard_match(const char *str, const char *expr)
{
   for (; *str && *expr; str++, expr++) {

      if (*expr == '*')
         return expr[1] == 0;

      if (*str != *expr && *expr != '?')
         return false; /* not a match */
   }

   return !*expr || (*expr == '*' && !expr[1]);
}

static int
handle_sys_trace_arg(const char *arg)
{
   struct bintree_walk_ctx ctx;
   struct symbol_node *n;
   bool match_sign = true;

   if (!*arg)
      return 0; /* empty string */

   if (*arg == '!') {

      if (!arg[1])
         return 0; /* empty negation string */

      match_sign = false;
      arg++;
   }

   bintree_in_order_visit_start(&ctx,
                                syms_bintree,
                                struct symbol_node,
                                node,
                                false);

   while ((n = bintree_in_order_visit_next(&ctx))) {

      if (simple_wildcard_match(n->name + 4, arg))
         traced_syscalls[n->sys_n] = match_sign;
   }

   return 0;
}

static int
set_traced_syscalls_int(const char *str)
{
   const size_t len = strlen(str);
   const char *s = str;
   char *p, buf[32];
   int rc;

   if (len >= TRACED_SYSCALLS_STR_LEN)
      return -ENAMETOOLONG;

   if (len == 0)
      return -EINVAL;

   for (p = buf; *s; s++) {

      if (p == buf + sizeof(buf))
         return -ENAMETOOLONG;

      if (*s == ',') {
         *p = 0;
         p = buf;

         if ((rc = handle_sys_trace_arg(buf)))
            return rc;

         continue;
      }

      *p++ = *s;
   }

   if (p > buf) {

      *p = 0;

      if ((rc = handle_sys_trace_arg(buf)))
         return rc;
   }

   traced_syscalls_count = 0;

   for (int i = 0; i < MAX_SYSCALLS; i++)
      if (traced_syscalls[i])
         traced_syscalls_count++;

   memcpy(traced_syscalls_str, str, len + 1);
   return 0;
}

static void
reset_traced_syscalls(void)
{
   for (int i = 0; i < MAX_SYSCALLS; i++)
      traced_syscalls[i] = false;
}

int
set_traced_syscalls(const char *s)
{
   int rc;
   disable_preemption();
   {
      reset_traced_syscalls();

      if ((rc = set_traced_syscalls_int(s)))
         reset_traced_syscalls();
   }
   enable_preemption();
   return rc;
}

static void
tracing_init_oom_panic(const char *buf_name)
{
   panic("Unable to allocate %s in init_tracing()", buf_name);
}

void
init_tracing(void)
{
   if (!(tracing_buf = kzmalloc(TRACE_BUF_SIZE)))
      tracing_init_oom_panic("tracing_buf");

   if (!(syms_buf = kmalloc(sizeof(struct symbol_node) * MAX_SYSCALLS)))
      tracing_init_oom_panic("syms_buf");

   if (!(syscalls_info = kzmalloc(sizeof(void *) * MAX_SYSCALLS)))
      tracing_init_oom_panic("syscalls_info");

   if (!(params_slots = kmalloc(sizeof(*params_slots))))
      tracing_init_oom_panic("params_slots");

   if (!(syscalls_fmts = kzmalloc(sizeof(s8) * MAX_SYSCALLS)))
      tracing_init_oom_panic("syscalls_fmts");

   if (!(traced_syscalls = kmalloc(MAX_SYSCALLS)))
      tracing_init_oom_panic("traced_syscalls");

   if (!(traced_syscalls_str = kmalloc(TRACED_SYSCALLS_STR_LEN)))
      tracing_init_oom_panic("traced_syscalls_str");

   ringbuf_init(&tracing_rb,
                TRACE_BUF_SIZE / sizeof(struct trace_event),
                sizeof(struct trace_event),
                tracing_buf);

   kmutex_init(&tracing_lock, 0);
   kcond_init(&tracing_cond);

   foreach_symbol(elf_symbol_cb, NULL);

   tracing_populate_syscalls_info();
   tracing_allocate_slots_for_params();

   set_traced_syscalls("*");
}

static struct module dp_module = {

   .name = "tracing",
   .priority = 100,
   .init = &init_tracing,
};

REGISTER_MODULE(&dp_module);
