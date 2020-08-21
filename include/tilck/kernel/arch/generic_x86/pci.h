/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

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

   u16 seg;       /* PCI Segment Group Number */
   u8 bus;        /* PCI Bus */
   u8 dev  : 5;   /* PCI Device Number */
   u8 func : 3;   /* PCI Function Number */
};

static ALWAYS_INLINE struct pci_device_loc
pci_make_loc(u16 seg, u8 bus, u8 dev, u8 func)
{
   return (struct pci_device_loc) {
      .seg = seg,
      .bus = bus,
      .dev = dev,
      .func = func
   };
}

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

const char *
pci_find_vendor_name(u16 id);

void
pci_find_device_class_name(struct pci_device_class *dev_class);

int
pci_config_read(struct pci_device_loc loc, u32 off, u32 width, u32 *val);

int
pci_config_write(struct pci_device_loc loc, u32 off, u32 width, u32 val);

void
init_pci(void);
