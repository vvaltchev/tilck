/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck_gen_headers/mod_acpi.h>
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/list.h>

enum acpi_init_status {

   ais_failed              = -1,
   ais_not_started         = 0,
   ais_tables_initialized  = 1,
   ais_tables_loaded       = 2,
   ais_subsystem_enabled   = 3,
   ais_fully_initialized   = 4,
};

#if MOD_acpi

static inline enum acpi_init_status
get_acpi_init_status(void)
{
   extern enum acpi_init_status acpi_init_status;
   return acpi_init_status;
}

void acpi_mod_init_tables(void);
void acpi_set_root_pointer(ulong);

#else

#define get_acpi_init_status()            ais_not_started
#define acpi_mod_init_tables()
#define acpi_set_root_pointer(...)

#endif

enum tristate acpi_is_8042_present(void);
enum tristate acpi_is_vga_text_mode_avail(void);

typedef u32 (*acpi_reg_callback)(void *);

struct acpi_reg_callback_node {

   struct list_node node;
   acpi_reg_callback cb;
   void *ctx;
};

/*
 * Register a callback that will be called immediately the ACPI subsystem has
 * been enabled at hardware level and we iterated through all the objects in
 * ACPI namespace.
 */
void acpi_reg_on_subsys_enabled_cb(struct acpi_reg_callback_node *node);

/*
 * Register a callback that will be called once ACPI has been fully initialized.
 *
 * NOTE: any errors returned by the callbacks are IGNORED. The only reason they
 * are required to return an integer it to reuse the same interface used by the
 * acpi_reg_on_subsys_enabled_cb() function.
 */
void acpi_reg_on_full_init_cb(struct acpi_reg_callback_node *node);


typedef u32 (*acpi_per_object_callback)(void *obj_handle,
                                        void *device_info,
                                        void *ctx);

struct acpi_reg_per_object_cb_node {

   struct list_node node;
   acpi_per_object_callback cb;
   void *ctx;

   const char *hid;  /* Required matching HID (if not NULL) */
   const char *uid;  /* Required matching UID (if not NULL) */
   const char *cls;  /* Required matching CLS (if not NULL) */

   /*
    * Optional filter function.
    * In case filtering using the fields above is not enough a filter function
    * pointer can be provided. The callback `cb` will be called only when the
    * filter has returned true.
    */
   bool (*filter)(void *obj_handle);
};

/*
 * Register a callback that will be called during the first iteration over all
 * the objects in the ACPI namespace, immediately after the ACPI subsystem has
 * been enabled.
 */
void acpi_reg_per_object_cb(struct acpi_reg_per_object_cb_node *cbnode);

/* Reboot the machine using the official ACPI method */
void acpi_reboot(void);

/* Power-off the machine (transition to state ACPI S5) */
void acpi_poweroff(void);

/* Get the average charge per mille of all batteries */
int acpi_get_all_batteries_charge_per_mille(ulong *ret);
