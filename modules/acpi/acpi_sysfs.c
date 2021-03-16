/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_sysfs.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/mods/acpi.h>
#include <tilck/mods/sysfs.h>
#include <tilck/mods/sysfs_utils.h>

#include "acpi_int.h"
#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/accommon.h>
#include <3rd_party/acpi/acinterp.h>

#if MOD_sysfs

#define PROP_NAME_BUF_SZ                         32
#define ACPI_SERIALIZE_MAX_DEPTH                  3

typedef offt (*acpi_serialize_func)(ACPI_OBJECT *, char *, offt, int);

static struct sysobj *dir_acpi_root;               /* /sysfs/acpi         */

static const char *acpi_types_str[ACPI_NUM_TYPES] = {

   [ACPI_TYPE_ANY] = "unknown",
   [ACPI_TYPE_INTEGER] = "integer",
   [ACPI_TYPE_STRING] = "string",
   [ACPI_TYPE_BUFFER] = "buffer",
   [ACPI_TYPE_PACKAGE] = "package",
   [ACPI_TYPE_FIELD_UNIT] = "field_unit",
   [ACPI_TYPE_DEVICE] = "device",
   [ACPI_TYPE_EVENT] = "event",
   [ACPI_TYPE_METHOD] = "method",
   [ACPI_TYPE_MUTEX] = "mutex",
   [ACPI_TYPE_REGION] = "region",
   [ACPI_TYPE_POWER] = "power",
   [ACPI_TYPE_PROCESSOR] = "processor",
   [ACPI_TYPE_THERMAL] = "thermal",
   [ACPI_TYPE_BUFFER_FIELD] = "buffer_field",
   [ACPI_TYPE_DDB_HANDLE] = "ddb_handle",
   [ACPI_TYPE_DEBUG_OBJECT] = "debug_object",
};

static offt
acpi_serialize_obj(ACPI_OBJECT *val, char *buf, offt buf_sz, int depth);

static offt
acpi_serialize_hex(ACPI_OBJECT *val, char *buf, offt buf_sz, int depth)
{
   if (val->Type != ACPI_TYPE_INTEGER)
      return 0;

   return snprintk(buf, buf_sz, "%*s%s%#" PRIx64 "\n",
                   depth * 2, "", depth ? "- " : "", val->Integer.Value);
}

static offt
acpi_serialize_ul_raw(u64 val, char *buf, offt buf_sz, int depth)
{
   return snprintk(buf, buf_sz, "%*s%s%" PRIu64 "\n",
                   depth * 2, "", depth ? "- " : "", val);
}

static offt
acpi_serialize_ul(ACPI_OBJECT *val, char *buf, offt buf_sz, int depth)
{
   return acpi_serialize_ul_raw(val->Integer.Value, buf, buf_sz, depth);
}

static offt
acpi_serialize_long_raw(u64 val, char *buf, offt buf_sz, int depth)
{
   return snprintk(buf, buf_sz, "%*s%s%" PRId64 "\n",
                   depth * 2, "", depth ? "- " : "", val);
}

static offt
acpi_serialize_long(ACPI_OBJECT *val, char *buf, offt buf_sz, int depth)
{
   return acpi_serialize_long_raw(val->Integer.Value, buf, buf_sz, depth);
}

static offt
acpi_serialize_eisaid(ACPI_OBJECT *val, char *buf, offt buf_sz, int depth)
{
   char tmpbuf[ACPI_EISAID_STRING_SIZE];
   AcpiExEisaIdToString(tmpbuf, val->Integer.Value);
   return snprintk(buf, buf_sz, "%s\n", tmpbuf);
}

static offt
acpi_serialize_str(ACPI_OBJECT *val, char *buf, offt buf_sz, int depth)
{
   return snprintk(buf, buf_sz, "%*s%s%s\n",
                   depth * 2, "", depth ? "- " : "", val->String.Pointer);
}

static offt
acpi_serialize_buf(ACPI_OBJECT *val, char *buf, offt buf_sz, int depth)
{
   return snprintk(buf, buf_sz, "%*s%s<buffer[%u]>\n",
                   depth * 2, "", depth ? "- " : "", val->Buffer.Length);
}

static offt
acpi_serialize_unknown(ACPI_OBJECT *val, char *buf, offt buf_sz, int depth)
{
   const char *type = "unknown";

   if (val->Type < ACPI_NUM_TYPES)
      type = acpi_types_str[val->Type];

   return snprintk(buf, buf_sz, "%*s%s<%s>\n",
                   depth * 2, "", depth ? "- " : "", type);
}

static offt
acpi_serialize_pkg(ACPI_OBJECT *val, char *buf, offt buf_sz, int depth)
{
   offt rc, written = 0;

   if (depth < ACPI_SERIALIZE_MAX_DEPTH) {

      rc = snprintk(buf + written, buf_sz - written, "%*s%sPackage:\n",
                    depth * 2, "", depth ? "- " : "");

   } else {

      rc = snprintk(buf + written,
                    buf_sz - written,
                    "%*s- <package>\n", depth * 2, "");

      return rc;
   }

   if (rc <= 0)
      return written;

   written += rc;

   for (u32 i = 0; i < val->Package.Count; i++) {

      rc = acpi_serialize_obj(&val->Package.Elements[i],
                              buf + written,
                              buf_sz - written,
                              depth + 1);

      if (rc <= 0)
         break;

      written += rc;
   }

   return written;
}

static offt
acpi_serialize_obj(ACPI_OBJECT *val, char *buf, offt buf_sz, int depth)
{
   static const acpi_serialize_func ser_funcs[ACPI_NUM_TYPES] = {
      [ACPI_TYPE_INTEGER] = &acpi_serialize_hex,
      [ACPI_TYPE_STRING] = &acpi_serialize_str,
      [ACPI_TYPE_BUFFER] = &acpi_serialize_buf,
      [ACPI_TYPE_PACKAGE] = &acpi_serialize_pkg,
   };

   acpi_serialize_func func = &acpi_serialize_unknown;

   if (val->Type < ACPI_NUM_TYPES && ser_funcs[val->Type])
      func = ser_funcs[val->Type];

   return func(val, buf, buf_sz, depth);
}

static offt
gen_data_load_handle_pidx(ACPI_OBJECT *val,
                          int pidx,
                          int bitx,
                          char fmt,
                          void *buf,
                          offt buf_sz)
{
   offt len = 0;

   if (pidx >= 0) {

      if (val->Type != ACPI_TYPE_PACKAGE || (u32)pidx >= val->Package.Count)
         return 0; /* Not a package: cannot get value at index `pdix` */

      /* Read the element at index `pdix` */
      val = &val->Package.Elements[pidx];
   }

   if (bitx >= 0) {

      if (val->Type == ACPI_TYPE_INTEGER) {

         len = acpi_serialize_ul_raw(
            !!(val->Integer.Value & (1 << bitx)), buf, buf_sz, 0
         );

      }

   } else {

      switch (fmt) {

         case 0: /* no fmt */
            if (val->Type == ACPI_TYPE_INTEGER)
               len = acpi_serialize_obj(val, buf, buf_sz, 0);
            break;

         case 'u': /* unsigned integer (base 10) */
            if (val->Type == ACPI_TYPE_INTEGER)
               len = acpi_serialize_ul(val, buf, buf_sz, 0);
            break;

         case 'x': /* unsigned integer (base 16) */
            if (val->Type == ACPI_TYPE_INTEGER)
               len = acpi_serialize_hex(val, buf, buf_sz, 0);
            break;

         case 'd': /* signed integer (base 10) */
            if (val->Type == ACPI_TYPE_INTEGER)
               len = acpi_serialize_long(val, buf, buf_sz, 0);
            break;

         case 'E': /* EISAID (ACPI HID in integer compressed form) */
            if (val->Type == ACPI_TYPE_INTEGER)
               len = acpi_serialize_eisaid(val, buf, buf_sz, 0);
            break;

         case 's': /* NUL-terminated string */
            if (val->Type == ACPI_TYPE_STRING)
               len = acpi_serialize_str(val, buf, buf_sz, 0);
            break;

         default:
            NOT_REACHED(); /* unknown fmt */
      }
   }

   return len;
}

static offt
acpi_generic_data_load(struct sysobj *s_obj,
                       void *child_expr,
                       void *buf,
                       offt buf_sz,
                       offt off)
{
   ACPI_HANDLE obj_handle = s_obj->extra;
   char child_buf[16] = {0};
   char *child = NULL;
   ACPI_OBJECT *val;
   ACPI_BUFFER res;
   int pidx = -1;
   int bitx = -1;
   offt len = 0;
   char fmt = 0;

   res.Length = ACPI_ALLOCATE_BUFFER;
   res.Pointer = NULL;

   if (child_expr && ((char *)child_expr)[0] != ':') {

      strncpy(child_buf, child_expr, sizeof(child_buf) - 1);
      child = child_buf;

      if (child[4] == '/') {

         const char *endptr;
         pidx = (int)tilck_strtoul(child + 5, &endptr, 10, NULL);

         if (*endptr) {

            /* There are non-numeric characters after the prop. index */

            if (*endptr == ':') /* bitfield */
               bitx = (int)tilck_strtoul(endptr + 1, NULL, 10, NULL);
            else
               fmt = *endptr; /* any other format char */
         }

         child[4] = 0;
      }

   } else if (child_expr && ((char *)child_expr)[0] == ':') {

      fmt = ((char *)child_expr)[1];
   }

   if (ACPI_SUCCESS(AcpiEvaluateObject(obj_handle, child, NULL, &res))) {

      val = res.Pointer;

      if (pidx >= 0 || fmt)
         len = gen_data_load_handle_pidx(val, pidx, bitx, fmt, buf, buf_sz);
      else
         len = acpi_serialize_obj(val, buf, buf_sz, 0);

      ACPI_FREE(res.Pointer);
   }

   return len;
}

static offt
acpi_prop_methods_load(struct sysobj *s_obj,
                       void *child_name,
                       void *buf,
                       offt buf_sz,
                       offt off)
{
   ACPI_HANDLE obj_handle = s_obj->extra;
   ACPI_HANDLE child = NULL;
   ACPI_STATUS rc;
   ACPI_BUFFER retbuf;
   offt written = 0;
   char name[8];
   int len;

   retbuf.Length = sizeof(name);
   retbuf.Pointer = name;

   while (true) {

      rc = AcpiGetNextObject(ACPI_TYPE_METHOD, obj_handle, child, &child);

      if (ACPI_FAILURE(rc))
         break;

      bzero(name, sizeof(name));

      if (ACPI_FAILURE(AcpiGetName(child, ACPI_SINGLE_NAME, &retbuf)))
         break;

      len = snprintk(buf + written, buf_sz - written, "%s ", name);

      if (len <= 0)
         break;

      written += len;
   }

   if (written > 0)
      written--; /* drop the trailing space */

   len = snprintk(buf + written, buf_sz - written, "\n");

   if (len > 0)
      written += len;

   return written;
}

/* Property types */
static struct sysobj_prop_type acpi_prop_type_generic_ro = {
   .load = &acpi_generic_data_load
};

static struct sysobj_prop_type acpi_prop_type_methods_ro = {
   .load = &acpi_prop_methods_load
};

/* Properties */
DEF_STATIC_SYSOBJ_PROP(type, &sysobj_ptype_ro_string_literal);
DEF_STATIC_SYSOBJ_PROP(value, &acpi_prop_type_generic_ro);
DEF_STATIC_SYSOBJ_PROP(methods, &acpi_prop_type_methods_ro);
DEF_STATIC_SYSOBJ_PROP(device_type, &sysobj_ptype_ro_string_literal);
DEF_STATIC_SYSOBJ_PROP(power_unit, &sysobj_ptype_ro_string_literal);
DEF_STATIC_SYSOBJ_PROP(design_capacity, &sysobj_ptype_ro_ulong_literal);
DEF_STATIC_SYSOBJ_PROP(capacity, &acpi_prop_type_generic_ro);
DEF_STATIC_SYSOBJ_PROP(rem_capacity, &acpi_prop_type_generic_ro);
DEF_STATIC_SYSOBJ_PROP(discharging, &acpi_prop_type_generic_ro);
DEF_STATIC_SYSOBJ_PROP(charging, &acpi_prop_type_generic_ro);
DEF_STATIC_SYSOBJ_PROP(critical_level, &acpi_prop_type_generic_ro);
DEF_STATIC_SYSOBJ_PROP(present_rate, &acpi_prop_type_generic_ro);

/* Sysfs obj types */
DEF_STATIC_SYSOBJ_TYPE(acpi_basic_sysobj_type,
                       &prop_type,
                       &prop_methods,
                       NULL);

DEF_STATIC_SYSOBJ_TYPE(acpi_data_sysobj_type,
                       &prop_type,
                       &prop_value,
                       NULL);

DEF_STATIC_SYSOBJ_TYPE(acpi_device_sysobj_type,
                       &prop_type,
                       &prop_methods,
                       NULL);

DEF_STATIC_SYSOBJ_TYPE(acpi_battery_sysobj_type,
                       &prop_type,
                       &prop_methods,
                       &prop_device_type,
                       &prop_power_unit,
                       &prop_design_capacity,
                       &prop_capacity,
                       &prop_rem_capacity,
                       &prop_discharging,
                       &prop_charging,
                       &prop_critical_level,
                       &prop_present_rate,
                       NULL);


static void
acpi_sysobj_data_dtor(ACPI_HANDLE obj, void *ctx)
{
   struct sysobj *s_obj = ctx;
   ASSERT(s_obj->extra == obj);
   s_obj->extra = NULL;
}

struct create_acpi_sys_obj_ctx {

   ACPI_HANDLE obj;
   ACPI_OBJECT_TYPE type;

   union {
      const char *name;
      const u32 *name_int;
   };

   const char *type_str;
   struct sysobj *view_dest_obj;
};

static struct sysobj *
create_ctrl_battery_sys_obj(struct create_acpi_sys_obj_ctx *ctx)
{
   const char *lfc_cap = "_BIF/2u"; /* last full charge capacity */
   const char *pu = "";
   ulong design_cap = 0xffffffff; /* unknown */
   struct basic_battery_info bi;

   if (ACPI_SUCCESS(acpi_battery_get_basic_info(ctx->obj, &bi))) {

      pu = bi.power_unit;

      if (bi.has_BIX)
         lfc_cap = "_BIX/3u";

      design_cap = bi.design_cap;
   }

   ctx->view_dest_obj = &sysfs_power_obj;

   return sysfs_create_obj(&acpi_battery_sysobj_type,
                           NULL,                  /* hooks */
                           ctx->type_str,         /* `type` */
                           NULL,                  /* `methods` */
                           "battery",             /* `device_type` */
                           pu,                    /* `power_unit` */
                           TO_PTR(design_cap),    /* `design_capacity` */
                           lfc_cap,               /* `capacity` */
                           "_BST/2u",             /* `rem_capacity` */
                           "_BST/0:0",            /* `discharging` */
                           "_BST/0:1",            /* `charging` */
                           "_BST/0:2",            /* `critical_level` */
                           "_BST/1u");            /* `present_rate` */
}

static struct sysobj *
create_acpi_device_sys_obj(struct create_acpi_sys_obj_ctx *ctx)
{
   if (acpi_is_battery(ctx->obj)) {
      /* We found a `Control Method Battery` device */
      return create_ctrl_battery_sys_obj(ctx);
   }

   /* Generic device */
   return sysfs_create_obj(&acpi_device_sysobj_type,
                           NULL,                  /* hooks */
                           ctx->type_str,         /* data for `type` */
                           NULL);                 /* data for `methods` */
}

static struct sysobj *
create_acpi_sys_obj(struct create_acpi_sys_obj_ctx *ctx)
{
   struct sysobj *s_obj = NULL;
   const char *child_expr = NULL;
   const char *type_str = ctx->type_str;

   switch (ctx->type) {

      case ACPI_TYPE_INTEGER:

         if (*ctx->name_int == READ_U32("_HID") ||
             *ctx->name_int == READ_U32("_CID"))
         {
            child_expr = ":E";
            type_str = "EISAID";
         }

         /* fall-through */

      case ACPI_TYPE_STRING:    /* fall-through */
      case ACPI_TYPE_BUFFER:    /* fall-through */
      case ACPI_TYPE_PACKAGE:
         s_obj = sysfs_create_obj(&acpi_data_sysobj_type,
                                  NULL,                 /* hooks */
                                  type_str,             /* data for `type` */
                                  child_expr);          /* data for `value` */

         break;

      case ACPI_TYPE_DEVICE:
         s_obj = create_acpi_device_sys_obj(ctx);
         break;

      default:
         s_obj = sysfs_create_obj(&acpi_basic_sysobj_type,
                                  NULL,                /* hooks */
                                  type_str,            /* data for `type` */
                                  NULL);               /* data for `methods` */
   }

   if (s_obj)
      s_obj->extra = ctx->obj;

   return s_obj;
}

static bool
should_skip_obj(ACPI_HANDLE parent, ACPI_HANDLE obj, ACPI_DEVICE_INFO *info)
{
   if (info->Type == ACPI_TYPE_METHOD)
      return true;

   return false;
}

ACPI_STATUS
register_acpi_obj_in_sysfs(ACPI_HANDLE parent_obj,
                           ACPI_HANDLE obj,
                           ACPI_DEVICE_INFO *obj_info)
{
   struct create_acpi_sys_obj_ctx ctx = {0};
   struct sysobj *s_parent = NULL;
   struct sysobj *s_obj;
   char name[8] = {0};
   ACPI_STATUS rc;

   if (parent_obj) {

      void *ptr;
      rc = AcpiGetData(parent_obj, &acpi_sysobj_data_dtor, &ptr);

      if (ACPI_SUCCESS(rc))
         s_parent = ptr;
   }

   if (obj) {

      ASSERT(obj_info);

      if (should_skip_obj(parent_obj, obj, obj_info))
         return AE_OK;

      memcpy(name, &obj_info->Name, sizeof(obj_info->Name));

      if (!s_parent)
         s_parent = dir_acpi_root;

      rc = AcpiGetData(obj, &acpi_sysobj_data_dtor, (void **)&s_obj);

      if (ACPI_SUCCESS(rc)) {

         /*
          * The `obj` handle points an ACPI_OPERAND_OBJECT we already travered
          * before, using another ACPI_NAMESPACE_NODE as handle. Therefore, we
          * already have a sysobj for this NS node: we need just a link.
          */

         if (sysfs_symlink_obj(NULL, s_parent, name, s_obj) < 0)
            return AE_NO_MEMORY;

         return AE_OK;
      }

      ctx.obj = obj;
      ctx.type = obj_info->Type;
      ctx.name = name;
      ctx.type_str = acpi_types_str[ACPI_TYPE_ANY];

      if (ctx.type < ARRAY_SIZE(acpi_types_str))
         ctx.type_str = acpi_types_str[ctx.type];

      s_obj = create_acpi_sys_obj(&ctx);

      if (!s_obj)
         return AE_NO_MEMORY;

      rc = AcpiAttachData(obj, &acpi_sysobj_data_dtor, s_obj);

      if (ACPI_FAILURE(rc)) {
         print_acpi_failure("AcpiAttachData", NULL, rc);
         sysfs_destroy_unregistered_obj(s_obj);
         return rc;
      }

   } else {

      ASSERT(!dir_acpi_root);
      dir_acpi_root = sysfs_create_empty_obj();

      if (!dir_acpi_root)
         return AE_NO_MEMORY;

      s_obj = dir_acpi_root;
      s_parent = &sysfs_root_obj;
      strcpy(name, "acpi");
   }

   if (name[3] == '_') {

      /*
       * Internally, ACPI objects with names shorter than 4 chars use a trailing
       * underscore character '_' to keep the length fixed, for example "_SB"
       * becomes "_SB_" and "EC0" become "EC0_". Since in the ACPI spec and in
       * the full paths generated by ACPICA the underscore is dropped, it is
       * worth dropping it from our view as well.
       */
      name[3] = 0;
   }

   if (sysfs_register_obj(NULL, s_parent, name, s_obj) < 0) {

      if (obj) {
         AcpiDetachData(obj, &acpi_sysobj_data_dtor);
         sysfs_destroy_unregistered_obj(s_obj);
      }

      return AE_NO_MEMORY;
   }

   if (ctx.view_dest_obj)
      sysfs_symlink_obj(NULL, ctx.view_dest_obj, name, s_obj);

   return AE_OK;
}

#else

ACPI_STATUS
register_acpi_obj_in_sysfs(ACPI_HANDLE parent_obj,
                           ACPI_HANDLE obj,
                           ACPI_DEVICE_INFO *obj_info)
{
   return AE_OK;
}

#endif
