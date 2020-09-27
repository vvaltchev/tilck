/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/mods/sysfs_utils.h>

#define DEF_STATIC_SYSOBJ_TYPE(_name, ...)                                \
                                                                          \
   static struct sysobj_prop *_name##_prop_list[] = { __VA_ARGS__ };      \
   static struct sysobj_type _name = {                                    \
      .name = #_name,                                                     \
      .properties = _name##_prop_list                                     \
   }

#define DEF_STATIC_SYSOBJ(_name, _type, _hooks,...)                       \
                                                                          \
   static void *_name##_data[] = { __VA_ARGS__ };                         \
   static struct sysobj _name = {                                         \
      .node = STATIC_LIST_NODE_INIT(_name.node),                          \
      .children_list = STATIC_LIST_INIT(_name.children_list),             \
      .type = _type,                                                      \
      .hooks = _hooks,                                                    \
      .prop_data = _name##_data,                                          \
   }

#define DEF_SHARED_EMPTY_SYSOBJ(_name)                                    \
                                                                          \
   struct sysobj _name = {                                                \
      .node = STATIC_LIST_NODE_INIT(_name.node),                          \
      .children_list = STATIC_LIST_INIT(_name.children_list),             \
   }

#define DEF_STATIC_SYSOBJ_PROP(_name, _type)                              \
   static struct sysobj_prop prop_##_name = {                             \
      .name = #_name,                                                     \
      .type = _type                                                       \
   }

#define DEF_STATIC_SYSOBJ_PROP2(_var_name, _prop_name, _type)             \
   static struct sysobj_prop _var_name = {                                \
      .name = #_prop_name,                                                \
      .type = _type                                                       \
   }

#define RO_STR_BUF_INIT(_value)                                           \
   {                                                                      \
      .buf = (char *)_value,                                              \
      .buf_sz = sizeof(_value)                                            \
   }

#define DEF_STATIC_RO_STR_BUF(_name, _value)                              \
   static struct sysfs_buffer _name = RO_STR_BUF_INIT(_value)

#define DEF_STATIC_CONF_RW_STRING(_name, _value, _max_len)                \
                                                                          \
   DEF_STATIC_SYSOBJ_PROP(_name, &sysobj_ptype_rw_config_str);            \
                                                                          \
   static char _name##_buf[_max_len] = _value;                            \
   static struct sysfs_buffer conf_##_name = {                            \
      .buf = _name##_buf,                                                 \
      .buf_sz = _max_len                                                  \
   }

#define DEF_STATIC_CONF_RO_STRING(_name, _value)                          \
                                                                          \
   DEF_STATIC_SYSOBJ_PROP(_name, &sysobj_ptype_ro_config_str);            \
   DEF_STATIC_RO_STR_BUF(conf_##_name, _value)

#define DEF_STATIC_CONF_RO_LONG(_name, _value)                            \
                                                                          \
   DEF_STATIC_SYSOBJ_PROP(_name, &sysobj_ptype_ro_long);                  \
   static long conf_##_name = _value

#define DEF_STATIC_CONF_RW_LONG(_name, _value)                            \
                                                                          \
   DEF_STATIC_SYSOBJ_PROP(_name, &sysobj_ptype_rw_long);                  \
   static long conf_##_name = _value

#define DEF_STATIC_CONF_RO_ULONG(_name, _value)                           \
                                                                          \
   DEF_STATIC_SYSOBJ_PROP(_name, &sysobj_ptype_ro_ulong);                 \
   static ulong conf_##_name = _value

#define DEF_STATIC_CONF_RW_ULONG(_name, _value)                           \
                                                                          \
   DEF_STATIC_SYSOBJ_PROP(_name, &sysobj_ptype_rw_ulong);                 \
   static ulong conf_##_name = _value

#define DEF_STATIC_CONF_RO_BOOL(_name, _value)                            \
                                                                          \
   DEF_STATIC_SYSOBJ_PROP(_name, &sysobj_ptype_ro_bool);                  \
   static bool conf_##_name = _value

#define DEF_STATIC_CONF_RW_BOOL(_name, _value)                            \
                                                                          \
   DEF_STATIC_SYSOBJ_PROP(_name, &sysobj_ptype_rw_bool);                  \
   static bool conf_##_name = _value

#define DEF_STATIC_CONF_RO(__type, __name, __value, ...)                  \
   CONCAT(DEF_STATIC_CONF_RO_,__type)(__name, __value, ##__VA_ARGS__)

#define DEF_STATIC_CONF_RW(__type, __name, __value, ...)                  \
   CONCAT(DEF_STATIC_CONF_RW_,__type)(__name, __value, ##__VA_ARGS__)

#define SYSOBJ_CONF_PROP_PAIR(name)                                       \
   &prop_##name, &conf_##name

/* Common property types */

struct sysfs_buffer {
   char *buf;
   u32 buf_sz;
   u32 used;
};

extern const struct sysobj_prop_type sysobj_ptype_ro_string_literal;
extern const struct sysobj_prop_type sysobj_ptype_ro_ulong_literal;
extern const struct sysobj_prop_type sysobj_ptype_ro_ulong_hex_literal;
extern const struct sysobj_prop_type sysobj_ptype_ulong;
extern const struct sysobj_prop_type sysobj_ptype_ro_ulong;
extern const struct sysobj_prop_type sysobj_ptype_long;
extern const struct sysobj_prop_type sysobj_ptype_ro_long;
extern const struct sysobj_prop_type sysobj_ptype_rw_bool;
extern const struct sysobj_prop_type sysobj_ptype_ro_bool;
extern const struct sysobj_prop_type sysobj_ptype_rw_config_str;
extern const struct sysobj_prop_type sysobj_ptype_ro_config_str;
extern const struct sysobj_prop_type sysobj_ptype_imm_data;
extern const struct sysobj_prop_type sysobj_ptype_databuf;
