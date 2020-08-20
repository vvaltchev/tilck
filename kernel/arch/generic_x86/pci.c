/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/errno.h>

#include "pci_classes.c.h"

#if KRN_PCI_VENDORS_LIST

   #include "pci_vendors.c.h"

#else

   const struct pci_vendor pci_vendors_list[] = {
      { 0xffff, "Illegal Vendor ID" }
   };

#endif

#define PCI_CONFIG_ADDRESS              0xcf8
#define PCI_CONFIG_DATA                 0xcfc


const char *
pci_find_vendor_name(u16 id)
{
   for (int i = 0; i < ARRAY_SIZE(pci_vendors_list); i++)
      if (pci_vendors_list[i].vendor_id == id)
         return pci_vendors_list[i].name;

   return NULL;
}

void
pci_find_device_class_name(struct pci_device_class *dev_class)
{
   int i;

   dev_class->class_name = NULL;
   dev_class->subclass_name = NULL;
   dev_class->progif_name = NULL;

   for (i = 0; i < ARRAY_SIZE(pci_device_classes_list); i++) {
      if (pci_device_classes_list[i].class_id == dev_class->class_id) {
         dev_class->class_name = pci_device_classes_list[i].class_name;
         break;
      }
   }

   if (!dev_class->class_name)
      return; /* PCI device class not found */

   /* Ok, we've found the device class, now look for the subclass */
   for (; i < ARRAY_SIZE(pci_device_classes_list); i++) {

      if (pci_device_classes_list[i].class_id != dev_class->class_id)
         break; /* it's pointless to search further */

      if (pci_device_classes_list[i].subclass_id == dev_class->subclass_id) {
         dev_class->subclass_name = pci_device_classes_list[i].subclass_name;
         break;
      }
   }

   if (!dev_class->subclass_name)
      return; /* PCI device sub-class not found */

   /* Ok, we've found both the class and the subclass. Look for a progif */
   for (; i < ARRAY_SIZE(pci_device_classes_list); i++) {

      if (pci_device_classes_list[i].subclass_id != dev_class->subclass_id)
         break; /* it's pointless to search further */

      if (pci_device_classes_list[i].progif_id == dev_class->progif_id) {
         dev_class->progif_name = pci_device_classes_list[i].progif_name;
         break;
      }
   }
}

int
pci_config_read(struct pci_device_loc loc, u32 off, u32 width, u32 *val)
{
   const u32 bus = loc.bus;
   const u32 dev = loc.dev;
   const u32 func = loc.func;
   const u32 aoff = off & ~3u;    /* off aligned at 4-byte boundary */
   const u32 addr = 0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | aoff;
   const u16 data_port = PCI_CONFIG_DATA + (off & 3);

   if (UNLIKELY(loc.seg != 0))
      return -EINVAL; /* Conventional PCI has no segment support */

   if (UNLIKELY(off >= 256 || off & ((width >> 3) - 1)))
      return -EINVAL;

   /* Write the address to the PCI config. space addr I/O port */
   outl(PCI_CONFIG_ADDRESS, addr);

   /* Read the data from the PCI config. space data I/O port */
   switch (width) {
      case 8:
         *val = inb(data_port);
         break;
      case 16:
         *val = inw(data_port);
         break;
      case 32:
         *val = inl(data_port);
         break;
      default:
         return -EINVAL;
   }

   return 0;
}

int
pci_config_write(struct pci_device_loc loc, u32 off, u32 width, u32 val)
{
   const u32 bus = loc.bus;
   const u32 dev = loc.dev;
   const u32 func = loc.func;
   const u32 aoff = off & ~3u;    /* off aligned at 4-byte boundary */
   const u32 addr = 0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | aoff;
   const u16 data_port = PCI_CONFIG_DATA + (off & 3);

   if (UNLIKELY(loc.seg != 0))
      return -EINVAL; /* Conventional PCI has no segment support */

   if (UNLIKELY(off >= 256 || off & ((width >> 3) - 1)))
      return -EINVAL;

   /* Write the address to the PCI config. space addr I/O port */
   outl(PCI_CONFIG_ADDRESS, addr);

   /* Write the data to the PCI config. space data I/O port */
   switch (width) {
      case 8:
         outb(data_port, (u8)val);
         break;
      case 16:
         outw(data_port, (u16)val);
         break;
      case 32:
         outl(data_port, (u32)val);
         break;
      default:
         return -EINVAL;
   }

   return 0;
}
