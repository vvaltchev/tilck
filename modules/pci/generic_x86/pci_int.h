/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck_gen_headers/mod_pci.h>

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/list.h>
#include <tilck/mods/pci.h>

#define PCI_CONFIG_ADDRESS              0xcf8
#define PCI_CONFIG_DATA                 0xcfc

#define PCI_DEV_BASE_INFO                0x00
#define PCI_CLASS_INFO_OFF               0x08
#define PCI_HDR_TYPE_OFF                 0x0e

#define PCI_HDR1_SECOND_BUS              0x19
#define PCI_HDR1_SUBORD_BUS              0x1a

#define BUS_NOT_VISITED                     0
#define BUS_TO_VISIT                        1
#define BUS_VISITED                         2

struct pci_segment {
   u64 base_paddr;
   u16 segment;
   u8 start_bus;
   u8 end_bus;
};

static u32 pcie_segments_cnt;
static struct pci_segment *pcie_segments;
static struct list pci_device_list;
static ulong (*pcie_get_conf_vaddr)(struct pci_device_loc);

static u8 *pci_buses;                  /* valid ONLY during init_pci() */
static ulong mmio_bus_va;              /* valid ONLY during init_pci() */
static struct pci_device_loc mmio_bus; /* valid if mmio_bus_va != 0 */

