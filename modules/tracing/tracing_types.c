/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * struct sys_param_type instances + the kernel-side `save` callbacks
 * that capture user-pointer data into a trace_event's saved-params
 * area at trace-emit time.
 *
 * The matching `dump` callbacks (text formatters) used to live here
 * too, but they moved to userspace `dp` (userapps/dp/tr_dump.c) when
 * trace-event rendering was extracted from the kernel. The userspace
 * tool reads /syst/tracing/metadata to learn the per-syscall slot
 * layout and dispatches its own dumps by enum tr_ptype_id.
 */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/user.h>
#include <tilck/kernel/sys_types.h>
#include <tilck/mods/tracing.h>

const struct sys_param_type ptype_int = {
   .name      = "int",
   .slot_size = 0,
   .ui_type   = ui_type_integer,
   .save      = NULL,
};

const struct sys_param_type ptype_voidp = {
   .name      = "void *",
   .slot_size = 0,
   .save      = NULL,
};

const struct sys_param_type ptype_oct = {
   .name      = "oct",
   .slot_size = 0,
   .ui_type   = ui_type_integer,
   .save      = NULL,
};

const struct sys_param_type ptype_errno_or_val = {
   .name      = "errno_or_val",
   .slot_size = 0,
   .ui_type   = ui_type_integer,
   .save      = NULL,
};

const struct sys_param_type ptype_errno_or_ptr = {
   .name      = "errno_or_ptr",
   .slot_size = 0,
   .save      = NULL,
};

const struct sys_param_type ptype_open_flags = {
   .name      = "int",
   .slot_size = 0,
   .save      = NULL,
};

const struct sys_param_type ptype_doff64 = {
   .name      = "ulong",
   .slot_size = 0,
   .ui_type   = ui_type_integer,
   .save      = NULL,
};

const struct sys_param_type ptype_whence = {
   .name      = "char *",
   .slot_size = 0,
   .save      = NULL,
};

/*
 * Saved-data layout for ptype_int32_pair: a validity flag followed by
 * the two ints copy_from_user'd at trace-emit time. Userspace dp's
 * tr_dump.c mirrors this layout when reading the slot back.
 */
struct saved_int_pair_data {
   bool valid;
   int  pair[2];
};

static bool
save_param_int_pair(void *data, long unused, char *dest_buf, size_t dest_bs)
{
   struct saved_int_pair_data *saved_data = (void *)dest_buf;
   ASSERT(dest_bs >= sizeof(struct saved_int_pair_data));

   if (copy_from_user(saved_data->pair, data, sizeof(int) * 2))
      saved_data->valid = false;
   else
      saved_data->valid = true;

   return true;
}

const struct sys_param_type ptype_int32_pair = {
   .name      = "int[2]",
   .slot_size = 32,
   .save      = save_param_int_pair,
};

static bool
save_param_u64_ptr(void *data, long unused, char *dest_buf, size_t dest_bs)
{
   ASSERT(dest_bs >= 8);
   u64 val;

   if (copy_from_user(&val, data, 8)) {
      snprintk(dest_buf, dest_bs, "<fault>");
      return true;
   }

   snprintk(dest_buf, dest_bs, "%llu", val);
   return true;
}

const struct sys_param_type ptype_u64_ptr = {
   .name      = "u64",
   .slot_size = 32,
   .ui_type   = ui_type_integer,
   .save      = save_param_u64_ptr,
};

const struct sys_param_type ptype_signum = {
   .name      = "signum",
   .slot_size = 0,
   .save      = NULL,
};

/* ------------------------------------------------------------------
 * Layer 1 — symbolic register-value ptypes
 *
 * No save callback (slot_size=0). The kernel only needs the instance
 * here so the metadata blob picks up the right enum tr_ptype_id at
 * /syst/tracing/metadata build time; the actual flag→string
 * formatting lives in userspace dp's tr_dump.c.
 * ------------------------------------------------------------------ */

const struct sys_param_type ptype_mmap_prot = {
   .name = "int", .slot_size = 0, .save = NULL,
};
const struct sys_param_type ptype_mmap_flags = {
   .name = "int", .slot_size = 0, .save = NULL,
};
const struct sys_param_type ptype_wait_options = {
   .name = "int", .slot_size = 0, .save = NULL,
};
const struct sys_param_type ptype_access_mode = {
   .name = "int", .slot_size = 0, .save = NULL,
};
const struct sys_param_type ptype_ioctl_cmd = {
   .name = "ioctl_cmd", .slot_size = 0, .save = NULL,
};
const struct sys_param_type ptype_fcntl_cmd = {
   .name = "fcntl_cmd", .slot_size = 0, .save = NULL,
};
const struct sys_param_type ptype_sigprocmask_how = {
   .name = "int", .slot_size = 0, .save = NULL,
};
const struct sys_param_type ptype_prctl_option = {
   .name = "int", .slot_size = 0, .save = NULL,
};
const struct sys_param_type ptype_clone_flags = {
   .name = "int", .slot_size = 0, .save = NULL,
};
const struct sys_param_type ptype_mount_flags = {
   .name = "int", .slot_size = 0, .save = NULL,
};
const struct sys_param_type ptype_madvise_advice = {
   .name = "int", .slot_size = 0, .save = NULL,
};
