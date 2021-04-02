/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_kernel.h>
#include <tilck/common/basic_defs.h>

#if KERNEL_UBSAN

/*
 * Struct definitions taken from GCC's source:
 *    libsanitizer/ubsan/ubsan_value.h
 *    libsanitizer/ubsan/ubsan_handlers.h
 */
struct TypeDescriptor {

   u16 TypeKind;
   u16 TypeInfo;
   char TypeName[];
};

struct SourceLocation {

  const char *Filename;
  u32 Line;
  u32 Column;
};

struct TypeMismatchDataV1 {

   struct SourceLocation Loc;
   const struct TypeDescriptor *Type;
   unsigned char LogAlignment;
   unsigned char TypeCheckKind;
};

static void
generic_ub_handler(const char *ub_name)
{
   if (in_panic())
      return;

   panic("UBSAN: %s", ub_name);
}

#define EMIT_GENERIC_UB_HANDLER(name) \
   void __ubsan_handle_##name() { generic_ub_handler(#name); }

void
__ubsan_handle_type_mismatch_v1(struct TypeMismatchDataV1 *d, ulong ptr)
{
   if (in_panic())
      return;

   if (!ptr) {

      panic("UBSAN: null pointer dereference\nUBSAN: %s:%u:%u",
            d->Loc.Filename, d->Loc.Line, d->Loc.Column);

   } else if (d->LogAlignment && (ptr & ((1 << d->LogAlignment) - 1))) {

      panic("UBSAN: unaligned access ptr: %p alignment: %u\nUBSAN: %s:%u:%u",
            ptr, 1 << d->LogAlignment,
            d->Loc.Filename, d->Loc.Line, d->Loc.Column);

   } else {

      panic("UBSAN: type_mismatch_v1\nUBSAN: %s:%u:%u",
            d->Loc.Filename, d->Loc.Line, d->Loc.Column);
   }
}

EMIT_GENERIC_UB_HANDLER(divrem_overflow)
EMIT_GENERIC_UB_HANDLER(shift_out_of_bounds)
EMIT_GENERIC_UB_HANDLER(builtin_unreachable)
EMIT_GENERIC_UB_HANDLER(missing_return)
EMIT_GENERIC_UB_HANDLER(vla_bound_not_positive)
EMIT_GENERIC_UB_HANDLER(type_mismatch)
EMIT_GENERIC_UB_HANDLER(type_mismatch_abort)
EMIT_GENERIC_UB_HANDLER(add_overflow)
EMIT_GENERIC_UB_HANDLER(sub_overflow)
EMIT_GENERIC_UB_HANDLER(mul_overflow)
EMIT_GENERIC_UB_HANDLER(negate_overflow)
EMIT_GENERIC_UB_HANDLER(load_invalid_value)
EMIT_GENERIC_UB_HANDLER(divrem_overflow_abort)
EMIT_GENERIC_UB_HANDLER(shift_out_of_bounds_abort)
EMIT_GENERIC_UB_HANDLER(vla_bound_not_positive_abort)
EMIT_GENERIC_UB_HANDLER(add_overflow_abort)
EMIT_GENERIC_UB_HANDLER(sub_overflow_abort)
EMIT_GENERIC_UB_HANDLER(mul_overflow_abort)
EMIT_GENERIC_UB_HANDLER(negate_overflow_abort)
EMIT_GENERIC_UB_HANDLER(load_invalid_value_abort)
EMIT_GENERIC_UB_HANDLER(float_cast_overflow)
EMIT_GENERIC_UB_HANDLER(float_cast_overflow_abort)
EMIT_GENERIC_UB_HANDLER(out_of_bounds)
EMIT_GENERIC_UB_HANDLER(out_of_bounds_abort)
EMIT_GENERIC_UB_HANDLER(nonnull_arg)
EMIT_GENERIC_UB_HANDLER(nonnull_arg_abort)
EMIT_GENERIC_UB_HANDLER(nonnull_return)
EMIT_GENERIC_UB_HANDLER(nonnull_return_abort)
EMIT_GENERIC_UB_HANDLER(dynamic_type_cache_miss)
EMIT_GENERIC_UB_HANDLER(dynamic_type_cache_miss_abort)

#endif // #if KERNEL_UBSAN
