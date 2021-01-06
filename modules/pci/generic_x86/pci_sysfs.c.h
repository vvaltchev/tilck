/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_sysfs.h>

#if MOD_sysfs

#include <tilck/mods/sysfs.h>
#include <tilck/mods/sysfs_utils.h>

static struct sysobj *dir_sysfs_pci;                /* /sysfs/pci           */

/* Properties */
DEF_STATIC_SYSOBJ_PROP(vendor_id, &sysobj_ptype_ro_ulong_hex_literal);
DEF_STATIC_SYSOBJ_PROP(device_id, &sysobj_ptype_ro_ulong_hex_literal);
DEF_STATIC_SYSOBJ_PROP(class_id, &sysobj_ptype_ro_ulong_hex_literal);
DEF_STATIC_SYSOBJ_PROP(subclass_id, &sysobj_ptype_ro_ulong_hex_literal);
DEF_STATIC_SYSOBJ_PROP(progif_id, &sysobj_ptype_ro_ulong_hex_literal);
DEF_STATIC_SYSOBJ_PROP(rev_id, &sysobj_ptype_ro_ulong_hex_literal);
DEF_STATIC_SYSOBJ_PROP(multi_func, &sysobj_ptype_ro_ulong_literal);
DEF_STATIC_SYSOBJ_PROP(class_name, &sysobj_ptype_ro_string_literal);
DEF_STATIC_SYSOBJ_PROP(subclass_name, &sysobj_ptype_ro_string_literal);
DEF_STATIC_SYSOBJ_PROP(progif_name, &sysobj_ptype_ro_string_literal);
DEF_STATIC_SYSOBJ_PROP(vendor_name, &sysobj_ptype_ro_string_literal);

/* Sysfs obj types */
DEF_STATIC_SYSOBJ_TYPE(pci_device_sysobj_type,
                       &prop_vendor_id,
                       &prop_device_id,
                       &prop_class_id,
                       &prop_subclass_id,
                       &prop_progif_id,
                       &prop_rev_id,
                       &prop_multi_func,
                       &prop_class_name,
                       &prop_subclass_name,
                       &prop_progif_name,
                       &prop_vendor_name,
                       NULL);

static int
pci_create_obj_for_device(struct pci_device *dev)
{
   struct pci_device_class dc = {0};
   struct pci_device_basic_info nfo;
   struct pci_device_loc loc;
   struct sysobj *obj, *parent;
   const char *vendor;
   char name[16];

   loc = dev->loc;
   nfo = dev->nfo;

   dc.class_id = nfo.class_id;
   dc.subclass_id = nfo.subclass_id;
   dc.progif_id = nfo.progif_id;

   pci_find_device_class_name(&dc);
   vendor = pci_find_vendor_name(nfo.vendor_id);

   obj = sysfs_create_obj(&pci_device_sysobj_type,
                          NULL,                    /* hooks */
                          TO_PTR(nfo.vendor_id),
                          TO_PTR(nfo.device_id),
                          TO_PTR(nfo.class_id),
                          TO_PTR(nfo.subclass_id),
                          TO_PTR(nfo.progif_id),
                          TO_PTR(nfo.rev_id),
                          TO_PTR(nfo.multi_func),
                          dc.class_name,
                          dc.subclass_name,
                          dc.progif_name,
                          vendor);

   if (!obj)
      return -ENOMEM;

   snprintk(name, sizeof(name), "%04x:%02x:%02x.%x",
            loc.seg, loc.bus, loc.dev, loc.func);

   if (sysfs_register_obj(NULL, dir_sysfs_pci, name, obj) < 0) {
      sysfs_destroy_unregistered_obj(obj);
      return -ENOMEM;
   }

   switch (nfo.class_id) {

      case PCI_CLASS_MASS_STORAGE:
         parent = &sysfs_storage_obj;
         break;
      case PCI_CLASS_NETWORK:
         parent = &sysfs_network_obj;
         break;
      case PCI_CLASS_DISPLAY:
         parent = &sysfs_display_obj;
         break;
      case PCI_CLASS_MULTIMEDIA:
         parent = &sysfs_media_obj;
         break;
      case PCI_CLASS_BRIDGE:
         parent = &sysfs_bridge_obj;
         break;
      case PCI_CLASS_COMMUNICATION:
         parent = &sysfs_comm_obj;
         break;
      case PCI_CLASS_GENERIC_PERIPHERAL:
         parent = &sysfs_genp_obj;
         break;
      case PCI_CLASS_INPUT:
         parent = &sysfs_input_obj;
         break;
      case PCI_CLASS_SERIAL_BUS:
         parent = &sysfs_serbus_obj;
         break;
      case PCI_CLASS_WIRELESS:
         parent = &sysfs_wifi_obj;
         break;
      case PCI_CLASS_SIGNAL_PROCESSING:
         parent = &sysfs_sigproc_obj;
         break;

      default:
         parent = &sysfs_other_obj;
         break;
   }

   return sysfs_symlink_obj(NULL, parent, name, obj);
}

static int
pci_create_sysfs_view(void)
{
   struct pci_device *dev;
   int rc = 0;

   dir_sysfs_pci = sysfs_create_empty_obj();

   if (!dir_sysfs_pci)
      return -ENOMEM;

   if (sysfs_register_obj(NULL, &sysfs_root_obj, "pci", dir_sysfs_pci) < 0) {
      sysfs_destroy_unregistered_obj(dir_sysfs_pci);
      return -ENOMEM;
   }

   list_for_each_ro(dev, &pci_device_list, node) {

      rc = pci_create_obj_for_device(dev);

      if (rc)
         break;
   }

   return rc;
}

#else

static int
pci_create_sysfs_view(void)
{
   return 0;
}

#endif
