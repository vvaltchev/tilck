/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/list.h>

#define PCI_CLASS_NONE                   0x00
#define PCI_CLASS_MASS_STORAGE           0x01
#define PCI_CLASS_NETWORK                0x02
#define PCI_CLASS_DISPLAY                0x03
#define PCI_CLASS_MULTIMEDIA             0x04
#define PCI_CLASS_MEMORY                 0x05
#define PCI_CLASS_BRIDGE                 0x06
#define PCI_CLASS_COMMUNICATION          0x07
#define PCI_CLASS_GENERIC_PERIPHERAL     0x08
#define PCI_CLASS_INPUT                  0x09
#define PCI_CLASS_DOCKING_STATION        0x0a
#define PCI_CLASS_PROCESSOR              0x0b
#define PCI_CLASS_SERIAL_BUS             0x0c
#define PCI_CLASS_WIRELESS               0x0d
#define PCI_CLASS_INTELLIGENT            0x0e
#define PCI_CLASS_SATELLITE_COMM         0x0f
#define PCI_CLASS_ENCRYPTION             0x10
#define PCI_CLASS_SIGNAL_PROCESSING      0x11
#define PCI_CLASS_PROC_ACC               0x12
#define PCI_CLASS_NON_ESSENTIAL_INST     0x13
#define PCI_CLASS_COPROC                 0x40
#define PCI_CLASS_UNASSIGNED             0xff

#define PCI_SUBCLASS_PCI_BRIDGE          0x04


struct pci_vendor {
   u16 vendor_id;
   const char *name;
};

struct pci_device_class {
   u8 class_id;
   u8 subclass_id;
   u8 progif_id;
   const char *class_name;
   const char *subclass_name;
   const char *progif_name;
};

struct pci_device_loc {

   union {

      struct {
         u16 seg;       /* PCI Segment Group Number */
         u8 bus;        /* PCI Bus */
         u8 dev  : 5;   /* PCI Device Number */
         u8 func : 3;   /* PCI Function Number */
      };

      u32 raw;
   };
};

struct pci_device_basic_info {

   union {

      struct {
         u16 vendor_id;
         u16 device_id;
      };

      u32 __dev_vendr;
   };

   union {

      struct {
         u8 rev_id;
         u8 progif_id;
         u8 subclass_id;
         u8 class_id;
      };

      u32 __class_info;
   };

   union {

      struct {
         u8 header_type : 7;
         u8 multi_func  : 1;
      };

      u8 __header_type;
   };
};

/*
 * PCI leaf node: a PCI device function.
 *
 * It's called just `device` and not `device_function` because it represents
 * a logical device, not a physical one. Also, `pci_device` is just shorter
 * and feels more natural than `pci_device_function` or `pci_device_node`.
 */
struct pci_device {

   struct list_node node;
   struct pci_device_loc loc;
   struct pci_device_basic_info nfo;
   void *ext_config;
};

static ALWAYS_INLINE struct pci_device_loc
pci_make_loc(u16 seg, u8 bus, u8 dev, u8 func)
{
   struct pci_device_loc ret;
   ret.seg  = seg;
   ret.bus  = bus;
   ret.dev  = dev  & 0b11111;
   ret.func = func & 0b00111;
   return ret;
}

const char *
pci_find_vendor_name(u16 id);

void
pci_find_device_class_name(struct pci_device_class *dev_class);

static inline int
pci_config_read(struct pci_device_loc loc, u32 off, u32 width, u32 *val)
{
   extern int (*__pci_config_read_func)(struct pci_device_loc, u32, u32, u32 *);
   return __pci_config_read_func(loc, off, width, val);
}

static inline int
pci_config_write(struct pci_device_loc loc, u32 off, u32 width, u32 val)
{
   extern int (*__pci_config_write_func)(struct pci_device_loc, u32, u32, u32);
   return __pci_config_write_func(loc, off, width, val);
}

struct pci_device *
pci_get_object(struct pci_device_loc loc);
