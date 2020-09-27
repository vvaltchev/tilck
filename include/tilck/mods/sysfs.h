/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/fs/vfs_base.h>

struct sysfs_inode;
struct sysfs_entry;
struct sysobj;

struct sysobj_prop_type {

   /*
    * Get the size of a buffer able to contain all the data returned by load().
    * This function can return values in the whole domain of `offt` (long).
    *
    *    > 0: means simply "buffer_size". In this case, the file backed by this
    *         property type will support seek(), but the contents of the file
    *         will be loaded only once, during open() and will be stored only
    *         once, during close(). This is suitable for big blobs of MUTABLE
    *         data. Example application: kernel coverage files.
    *
    *    = 0: means there's no buffer. In this case, read() and write(0 on the
    *         file backed by this property type will call directly load() and
    *         store(), every time. No seek() support whatsoever. This is the
    *         typical case for small properties like boolean or integer config
    *         options. Very common. Example application: kernel config flags.
    *
    *    < 0: means "- buffer_size", but does NOT require the allocation of a
    *         dedicated buffer during open(). It allows seek() though, because
    *         the backing data is assumed to be completely IMMUTABLE, once the
    *         sysobj is created. Example application: kernel symbol table.
    */
   offt (*get_buf_sz)(struct sysobj *obj, void *data);

   /*
    * Load the property data at offset `off` into the `buf` buffer, assumed to
    * be at least `sz` bytes large. Note: the `off` argument can be > 0
    * only when get_buf_sz() returned a value < 0. In all the other cases, it
    * must be 0.
    *
    * Returns the number of bytes loaded into `buf` or an error in case the
    * value is < 0.
    */
   offt (*load)(struct sysobj *obj, void *data, void *buf, offt sz, offt off);

   /*
    * Store data into `buf` into the property rappresented by `prop_data`.
    * The `buf` buffer is assumed to be at least `sz` large.
    *
    * Returns the number of bytes actually read from `buf` or an error in case
    * the value is < 0.
    */
   offt (*store)(struct sysobj *obj, void *data, void *buf, offt sz);

   /*
    * Get the underlying data pointer (allows mmap to work).
    * At the moment, that is supported only when get_buf_sz() returns < 0.
    */
   void *(*get_data_ptr)(struct sysobj *obj, void *data);
};

struct sysobj_prop {

   const char *name;                /* actual name of a file in sysfs */
   const struct sysobj_prop_type *type;
};

struct sysobj_type {

   const char *name;                /* internal name, not visible in sysfs */
   struct sysobj_prop **properties; /* pointer to a NULL-terminated array of
                                       pointers to sysobj_prop objects */
};

struct sysobj_hooks {

   offt (*pre_load)(struct sysobj *, struct sysobj_prop *, void *);
   void (*post_load)(struct sysobj *, struct sysobj_prop *, void *);
   offt (*pre_store)(struct sysobj *, struct sysobj_prop *, void *);
   void (*post_store)(struct sysobj *, struct sysobj_prop *, void *);
};

struct sysobj {

   /* Sysfs public fields */
   struct list_node node;
   struct list children_list;
   struct sysobj_type *type;
   struct sysobj_hooks *hooks;
   void **prop_data;

   /* Fields completely managed by the upper layers */
   void *extra;

   /* Sysfs internal fields */
   bool prop_data_owned;
   bool type_owned;
   bool object_owned; /* sysfs allocated the object */
   struct sysfs_inode *inode;
   struct sysfs_entry *entry;
   struct sysobj *parent;
};

/*
 * Default objects of the main sysfs instance, the one created by init_sysfs().
 * See the comments above create_sysfs().
 */
extern struct sysobj sysfs_root_obj;
extern struct sysobj sysfs_hw_obj;
extern struct sysobj sysfs_power_obj;
extern struct sysobj sysfs_storage_obj;
extern struct sysobj sysfs_network_obj;
extern struct sysobj sysfs_display_obj;
extern struct sysobj sysfs_media_obj;
extern struct sysobj sysfs_bridge_obj;
extern struct sysobj sysfs_comm_obj;
extern struct sysobj sysfs_genp_obj;
extern struct sysobj sysfs_input_obj;
extern struct sysobj sysfs_serbus_obj;
extern struct sysobj sysfs_wifi_obj;
extern struct sysobj sysfs_sigproc_obj;
extern struct sysobj sysfs_other_obj;

/*
 * Create a sysfs instance.
 *
 * Note: Tilck's sysfs is DIFFERENT from Linux kernel's sysfs. Indeed, it can
 * and will be used for several different purposes:
 *
 *    - Provide a Tilck specific /syst
 *    - Provide a minimal Linux-compatibile /proc (future)
 *    - Provide a minimal Linux-compatibile /sys  (future)
 *    - Maybe replace Tilck's devfs (future)
 */

struct fs *
create_sysfs(void);

/*
 * Basic function for initializing a caller-allocated sysobj.
 *
 * NOTE: when the sysobj is destroyed, the caller is responsible for freeing
 * the `type` and the `prop_data` objects. In other words, the ownership is not
 * transferred to the sysfs layer.
 */
void
sysobj_init(struct sysobj *obj,
            struct sysobj_type *type,
            struct sysobj_hooks *hooks,
            void **prop_data);

/*
 * Dynamically create a sysobj of type `type` having the values of its
 * properties specified as varargs. Uses dynamic property data.
 *
 * Note[1]: the number of varargs (after `hooks`) must be exactly as the number
 * of properties of `type`.
 *
 * Note[2]: the ownership of `type`, `hooks` and `prop_data` is NOT transferred
 * to sysfs.
 */

struct sysobj *
sysfs_create_obj(struct sysobj_type *type, struct sysobj_hooks *hooks, ...);


/*
 * va_list variant of sysfs_create_obj(), used mostly internally.
 * Note: the ownership of `type` and `hooks` is NOT transferred to sysfs.
 */
struct sysobj *
sysfs_create_obj_va(struct sysobj_type *type,
                    struct sysobj_hooks *hooks,
                    va_list args);

/*
 * Base variant of sysfs_create_obj().
 *
 * Note: the ownership of `type`, `hooks` and `prop_data` is NOT transferred
 * to sysfs.
 */
struct sysobj *
sysfs_create_obj_va_arr(struct sysobj_type *type,
                        struct sysobj_hooks *hooks,
                        void **prop_data);

/*
 * Create a sys object with a dedicated type, created on the fly.
 *
 * This function is very useful to create unique objects that don't share their
 * type with any other sys object. The first argument is the name of the obj's
 * type (internal-only field, not visible from userland). Then, it follows a
 * list of pairs <property, property data> and the whole thing ends up with a
 * trailing NULL. In case the properties are static config properties defined
 * with the DEF_STATIC_CONF_* macros, using SYSOBJ_CONF_PROP_PAIR() will make
 * this function even simpler to use. See config.c.
 *
 * Note: the ownership of `hooks` is NOT transferred to sysfs.
 */
struct sysobj *
sysfs_create_custom_obj(const char *type_name, struct sysobj_hooks *hooks, ...);


/*
 * va_list variant of sysfs_create_custom_obj(), used mostly internally.
 * Note: the ownership of `hooks` is NOT transferred to sysfs.
 */
struct sysobj *
sysfs_create_custom_obj_va(const char *type_name,
                           struct sysobj_hooks *hooks,
                           u32 args_cnt,
                           va_list args);

/*
 * Create an empty sys object. Basically, just a directory in sysfs.
 */
struct sysobj *
sysfs_create_empty_obj(void);

/*
 * Destroy a not-yet-registered sys object.
 *
 * It's supposed to be used in error code-paths. The destruction of registered
 * objects is NOT supported.
 *
 * Details: this function might also free the type object (along with its
 * `properties` array of pointers) AND the `prop_data` array as well, depending
 * on how the object has been created.
 *
 * The general rule (always valid)
 * ---------------------------------
 *
 * If the caller of the function used to create the object passed to it a type
 * pointer, that type WILL NOT be freed. Otherwise, if the type has been created
 * by the create function (e.g. sysfs_create_custom_obj()), then it will be
 * freed.
 *
 * The same rule applies to `prop_data` array: often, it's allocated by the
 * create function and there will be freed by this function. In the special
 * cases (e.g. sysfs_create_obj_va_arr) where the caller passed the `prop_data`
 * array to the sysfs create function, the `prop_data` will not be freed.
 *
 * In the most generic case where the sys object has been created outside of
 * sysfs and it has been initialized with sysobj_init(), clearly the caller owns
 * everything and MUST NOT call sysfs_destroy_unregistered_obj().
 */
void
sysfs_destroy_unregistered_obj(struct sysobj *obj);

/*
 * Register a sys object in the `fs` sysfs instance, under the object `parent`.
 *
 * Notes:
 *
 *    1. In order to register an object into the main sysfs instance, pass
 *       NULL to `fs`.
 *
 *    2. In order to register the root object, pass NULL to parent.
 *
 *    3. The main sysfs instace has always a root object and it's called:
 *       sysfs_root_obj (see below).
 *
 *    4. The contents of `name` are copied into sysfs. The buffer is NOT
 *       required to exist after this call.
 */

int
sysfs_register_obj(struct fs *fs,
                   struct sysobj *parent,
                   const char *name,
                   struct sysobj *obj);

/*
 * Sym-link a sysobj under a different parent with a different name.
 */

int
sysfs_symlink_obj(struct fs *fs,
                  struct sysobj *new_parent,
                  const char *new_name,
                  struct sysobj *obj);

/*
 * Create and initialize Tilck's main sysfs instance, /syst.
 */

void
init_sysfs(void);

