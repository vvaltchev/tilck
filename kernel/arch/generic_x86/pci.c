/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include "pci_classes.c.h"

#if KRN_PCI_VENDORS_LIST

   #include "pci_vendors.c.h"

#else

   const struct pci_vendor pci_vendors_list[] = {
      { 0xffff, "Illegal Vendor ID" }
   };

#endif

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
