/* SPDX-License-Identifier: BSD-2-Clause */

/*                literal string             */

static offt
sys_str_literal_load(struct sysobj *obj,
                     void *data, void *buf, offt buf_sz, offt off)
{
   ASSERT(off == 0);

   if (data)
      return snprintk(buf, (size_t)buf_sz, "%s\n", data);

   return 0;
}

const struct sysobj_prop_type sysobj_ptype_ro_string_literal = {
   .load = &sys_str_literal_load
};

/*                literal ulong             */

static offt
sys_ulong_literal_load(struct sysobj *obj,
                       void *data, void *buf, offt buf_sz, offt off)
{
   ASSERT(off == 0);
   return snprintk(buf, (size_t)buf_sz, "%lu\n", (ulong)data);
}

const struct sysobj_prop_type sysobj_ptype_ro_ulong_literal = {
   .load = &sys_ulong_literal_load
};

/*                literal ulong_hex             */

static offt
sys_ulong_hex_literal_load(struct sysobj *obj,
                           void *data, void *buf, offt buf_sz, offt off)
{
   ASSERT(off == 0);
   return snprintk(buf, (size_t)buf_sz, "%#lx\n", (ulong)data);
}

const struct sysobj_prop_type sysobj_ptype_ro_ulong_hex_literal = {
   .load = &sys_ulong_hex_literal_load
};

/*                ulong             */

static offt
sys_ulong_load(struct sysobj *obj, void *data, void *buf, offt buf_sz, offt off)
{
   ASSERT(off == 0);
   const ulong val = *(ulong *)data;
   return snprintk(buf, (size_t)buf_sz, "%lu\n", val);
}

static offt
sys_ulong_store(struct sysobj *obj, void *data, void *buf, offt buf_sz)
{
   int err = 0;
   ulong val;
   char tmp[32] = {0};
   memcpy(tmp, buf, (size_t)MAX(buf_sz, 31));

   val = tilck_strtoul(tmp, NULL, 10, &err);

   if (!err)
      *(ulong *)data = val;

   /*
    * We MUST always return `buf_sz` to make sure userland code believes the
    * whole buffer was consumed. Otherwise, it will get stuck re-trying forever
    * to write to this property (which from userland is just a regular file).
    */
   return buf_sz;
}

const struct sysobj_prop_type sysobj_ptype_rw_ulong = {
   .load = &sys_ulong_load,
   .store = &sys_ulong_store,
};

const struct sysobj_prop_type sysobj_ptype_ro_ulong = {
   .load = &sys_ulong_load,
};


/*                long             */

static offt
sys_long_load(struct sysobj *obj, void *data, void *buf, offt buf_sz, offt off)
{
   ASSERT(off == 0);
   const long val = *(long *)data;
   return snprintk(buf, (size_t)buf_sz, "%ld\n", val);
}

static offt
sys_long_store(struct sysobj *obj, void *data, void *buf, offt buf_sz)
{
   int err = 0;
   long val;
   char tmp[32] = {0};
   memcpy(tmp, buf, (size_t)MAX(buf_sz, (offt)sizeof(tmp) - 1));

   val = tilck_strtol(tmp, NULL, 10, &err);

   if (!err)
      *(long *)data = val;

   /*
    * We MUST always return `buf_sz` to make sure userland code believes the
    * whole buffer was consumed. Otherwise, it will get stuck re-trying forever
    * to write to this property (which from userland is just a regular file).
    */
   return buf_sz;
}

const struct sysobj_prop_type sysobj_ptype_rw_long = {
   .load = &sys_long_load,
   .store = &sys_long_store,
};

const struct sysobj_prop_type sysobj_ptype_ro_long = {
   .load = &sys_long_load,
   .store = NULL,
};

/*               bool             */

static offt
sys_bool_load(struct sysobj *obj, void *data, void *buf, offt buf_sz, offt off)
{
   ASSERT(off == 0);
   return snprintk(buf, (size_t)buf_sz, "%u\n", *(bool *)data);
}

static offt
sys_bool_store(struct sysobj *obj, void *data, void *buf, offt buf_sz)
{
   char *s = buf;

   if (s[0] == '0' || s[1] == '1') {
      if (!s[1] || s[1] == '\n' || s[1] == '\r') {
         *(bool *)data = s[0] - '0';
      }
   }

   return buf_sz;
}

const struct sysobj_prop_type sysobj_ptype_rw_bool = {
   .load = &sys_bool_load,
   .store = &sys_bool_store,
};

const struct sysobj_prop_type sysobj_ptype_ro_bool = {
   .load = &sys_bool_load,
};


/*                config string             */

static offt
sys_config_str_load(struct sysobj *obj,
                    void *data, void *buf, offt buf_sz, offt off)
{
   ASSERT(off == 0);
   struct sysfs_buffer *str = data;
   return (offt)snprintk(buf, (size_t)buf_sz, "%s\n", str->buf);
}

static offt
sys_config_str_store(struct sysobj *obj, void *data, void *buf, offt buf_sz)
{
   struct sysfs_buffer *str = data;
   offt len = 0, lim = MIN(buf_sz, (offt)str->buf_sz - 1);
   char *dest = str->buf;

   for (char *p = buf; *p && *p != '\n' && len < lim; p++, len++) {
      *dest++ = *p;
   }

   *dest = 0;
   return buf_sz;
}

const struct sysobj_prop_type sysobj_ptype_rw_config_str = {
   .load = &sys_config_str_load,
   .store = &sys_config_str_store,
};

const struct sysobj_prop_type sysobj_ptype_ro_config_str = {
   .load = &sys_config_str_load,
};

/*                immutable data             */

offt
sysobj_imm_data_get_buf_sz(struct sysobj *obj, void *prop_data)
{
   struct sysfs_buffer *buf = prop_data;
   return -(offt)buf->buf_sz;
}

void *
sysobj_imm_data_get_data_ptr(struct sysobj *obj, void *prop_data)
{
   struct sysfs_buffer *databuf = prop_data;
   return databuf->buf;
}

static offt
sys_imm_data_load(struct sysobj *obj,
                  void *prop_data, void *buf, offt buf_sz, offt off)
{
   struct sysfs_buffer *databuf = prop_data;
   offt to_read;

   /*
    * This is the only case where `off` is allowed to be > 0.
    * Note: we're returning -size in our get_buf_sz() func.
    */

   off = CLAMP(off, 0, (offt)databuf->buf_sz);
   to_read = MIN((offt)databuf->buf_sz - off, buf_sz);
   memcpy(buf, databuf->buf + off, (size_t)to_read);
   return to_read;
}

const struct sysobj_prop_type sysobj_ptype_imm_data = {
   .get_buf_sz = &sysobj_imm_data_get_buf_sz,
   .load = &sys_imm_data_load,
   .get_data_ptr = &sysobj_imm_data_get_data_ptr,
};


/*                mutable data buffer              */


offt
sysobj_databuf_get_buf_sz(struct sysobj *obj, void *prop_data)
{
   struct sysfs_buffer *buf = prop_data;
   return (offt)buf->buf_sz;
}

static offt
sys_databuf_load(struct sysobj *obj,
                 void *prop_data, void *dest, offt dest_buf_sz, offt off)
{
   ASSERT(off == 0);
   struct sysfs_buffer *databuf = prop_data;
   offt to_read = MIN(dest_buf_sz, (offt)databuf->used);
   memcpy(dest, databuf->buf, (size_t)to_read);
   return to_read;
}

static offt
sys_databuf_store(struct sysobj *obj,
                  void *prop_data, void *src, offt src_buf_sz)
{
   struct sysfs_buffer *databuf = prop_data;
   offt to_write = MIN(src_buf_sz, (offt)databuf->buf_sz);
   memcpy(databuf->buf, src, (size_t)to_write);
   databuf->used = (u32)to_write;
   return src_buf_sz;
}

const struct sysobj_prop_type sysobj_ptype_databuf = {
   .get_buf_sz = &sysobj_databuf_get_buf_sz,
   .load = &sys_databuf_load,
   .store = &sys_databuf_store,
};
