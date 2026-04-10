/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Sources:
 *   https://wiki.osdev.org/Intel_8254x
 *   https://www.intel.com/content/dam/doc/manual/
 *      pci-pci-x-family-gbe-controllers-software-dev-manual.pdf
 */

#include <tilck/common/utils.h>
#include <tilck/common/printk.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/net.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/modules.h>
#include <tilck/kernel/worker_thread.h>
#include <tilck/kernel/irq.h>
#include <tilck/mods/pci.h>
#include <tilck/mods/tracing.h>

#include "e1000_config.h"

/*
 * Inferred from the configuration in e1000_config.
 * Don't change these directly.
 */

#define TX_RING_BYTES   (sizeof(struct tx_desc) * TX_RING_CAP)
#define TX_RING_PAGES   DIV_ROUND_UP(TX_RING_BYTES, PAGE_SIZE)

#define TX_DATA_BYTES   (TX_BUF_SIZE * TX_RING_CAP)
#define TX_DATA_PAGES   DIV_ROUND_UP(TX_DATA_BYTES, PAGE_SIZE);

#define RX_RING_BYTES   (sizeof(struct rx_desc) * RX_RING_CAP)
#define RX_RING_PAGES   DIV_ROUND_UP(RX_RING_BYTES, PAGE_SIZE)

#define RX_DATA_BYTES   (RX_BUF_SIZE * RX_RING_CAP)
#define RX_DATA_PAGES   DIV_ROUND_UP(RX_DATA_BYTES, PAGE_SIZE);

/*
 * Device versions
 */

#define VER_82547EI_A0  1
#define VER_82547EI_A1  2
#define VER_82547EI_B0  3
#define VER_82547GI_B0  4
#define VER_82546EB_A1  5
#define VER_82546GB_B0  6
#define VER_82545EM_A   7
#define VER_82545GM_B   8
#define VER_82544EI_A4  9
#define VER_82544GC_A4  10
#define VER_82541EI_A0  11
#define VER_82541EI_B0  12
#define VER_82541GI_B1  13
#define VER_82541PI_C0  14
#define VER_82541ER_C0  15
#define VER_82540EP_A   16
#define VER_82540EM_A   17

/*
 * PCI Configuration Registers
 */

#define PCI_REG_INTR_LINE 0x3C
#define PCI_REG_CMD       0x04
#define PCI_REG_BAR0      0x10
#define PCI_REG_BAR1      0x14

/*
 * Memory-Mapped Device Registers
 */

#define REG_CTRL  0x0000
#define REG_EECD  0x0010
#define REG_EERD  0x0014
#define REG_ICR   0x00C0
#define REG_IMS   0x00D0
#define REG_RCTL  0x0100
#define REG_RDBAL 0x2800
#define REG_RDBAH 0x2804
#define REG_RDLEN 0x2808
#define REG_RDH   0x2810
#define REG_RDT   0x2818
#define REG_TCTL  0x0400
#define REG_TDBAL 0x3800
#define REG_TDBAH 0x3804
#define REG_TDLEN 0x3808
#define REG_TDH   0x3810
#define REG_TDT   0x3818
#define REG_RAL   0x5400
#define REG_RAH   0x5404
#define REG_ICS   0x00C8
#define REG_IMC   0x00D8

/*
 * PCI Configuration Register Bits
 */

#define BIT_PCI_REG_CMD_BME (1 << 2) /* Bus Master Enable */
#define BIT_PCI_REG_CMD_CID (1 << 10) /* Clear Interrupt Disable */

/*
 * Memory-Mapped Device Register Bits
 */

#define BIT_CTRL_FD      (1 << 0)
#define BIT_CTRL_LRST    (1 << 3)
#define BIT_CTRL_ASDE    (1 << 5)
#define BIT_CTRL_SLU     (1 << 6)
#define BIT_CTRL_ILOS    (1 << 7)
#define BIT_CTRL_RST     (1 << 26)
#define BIT_CTRL_RFCE    (1 << 27)
#define BIT_CTRL_TFCE    (1 << 28)
#define BIT_CTRL_VME     (1 << 30)
#define BIT_CTRL_PHY_RST (1 << 31)

#define BIT_EECD_SK      (1 << 0)
#define BIT_EECD_CS      (1 << 1)
#define BIT_EECD_DI      (1 << 2)
#define BIT_EECD_EE_REQ  (1 << 6)
#define BIT_EECD_EE_GNT  (1 << 7)
#define BIT_EECD_EE_PRES (1 << 8)

#define BIT_EERD_START   (1 << 0)
#define BIT_EERD_DONE    (1 << 4)

#define BIT_IMS_TXDW     (1 << 0)
#define BIT_IMS_TXQE     (1 << 1)
#define BIT_IMS_LSC      (1 << 2)
#define BIT_IMS_RXSEQ    (1 << 3)
#define BIT_IMS_RXDMT0   (1 << 4)
#define BIT_IMS_RXO      (1 << 6)
#define BIT_IMS_RXT0     (1 << 7)
#define BIT_IMS_MDAC     (1 << 9)
#define BIT_IMS_RXCFG    (1 << 10)
#define BIT_IMS_PHYINT   (1 << 12)
#define BIT_IMS_TXD_LOW  (1 << 15)
#define BIT_IMS_SRPD     (1 << 16)

#define BIT_TCTL_EN      (1 << 1)
#define BIT_TCTL_PSP     (1 << 3)

#define BIT_RCTL_EN      (1 << 1)
#define BIT_RCTL_SBP     (1 << 2)
#define BIT_RCTL_UPE     (1 << 3)
#define BIT_RCTL_MPE     (1 << 4)
#define BIT_RCTL_LPE     (1 << 5)
#define BIT_RCTL_BAM     (1 << 15)
#define BIT_RCTL_VFE     (1 << 18)
#define BIT_RCTL_CFIEN   (1 << 19)
#define BIT_RCTL_CFI     (1 << 20)
#define BIT_RCTL_DPF     (1 << 22)
#define BIT_RCTL_PMCF    (1 << 23)
#define BIT_RCTL_BSEX    (1 << 25)
#define BIT_RCTL_SECRC   (1 << 26)

#define BIT_RX_STATUS_DD  (1 << 0)
#define BIT_RX_STATUS_EOP (1 << 1)

/*
 * Transmission Descriptor Command Flags
 */

#define TX_DESC_CMD_EOP  (1 << 0)
#define TX_DESC_CMD_IFCS (1 << 1)
#define TX_DESC_CMD_IC   (1 << 2)
#define TX_DESC_CMD_RS   (1 << 3)
#define TX_DESC_CMD_RPS  (1 << 4)
#define TX_DESC_CMD_DEXT (1 << 5)
#define TX_DESC_CMD_VLE  (1 << 6)
#define TX_DESC_CMD_IDE  (1 << 7)

/*
 * Types
 */

struct tx_desc {
   u64 addr;
   u16 length;
   u8  checksum_offset;
   u8  command;
   u8  status;
   u8  checksum_start;
   u16 special;
};

struct rx_desc {
   u64 addr;
   u16 length;
   u16 checksum;
   u8  status;
   u8  errors;
   u16 special;
};

STATIC_ASSERT(sizeof(struct tx_desc) == 16); /* Ensure tightly packed */
STATIC_ASSERT(sizeof(struct rx_desc) == 16); /* ... */

/*
 * Globals
 */

static struct worker_thread *wth;
static int device_version;
static struct mac_addr mac;
static struct tx_desc *tx_ring;
static struct rx_desc *rx_ring;
static char *tx_data;
static char *rx_data;
static u32   tx_tail;
static u32   rx_tail;
static ulong io_addr;
static bool  is_mmio;

/*
 * Table of supported devices
 */

struct {
   int version;
   u16 vendor_id;
   u16 device_id;
} device_table[] = {
   { VER_82547EI_A0, 0x8086, 0x1019 },
   { VER_82547EI_A1, 0x8086, 0x1019 },
   { VER_82547EI_B0, 0x8086, 0x1019 },
   { VER_82547EI_B0, 0x8086, 0x101A },
   { VER_82547GI_B0, 0x8086, 0x1019 },
   { VER_82546EB_A1, 0x8086, 0x1010 },
   { VER_82546EB_A1, 0x8086, 0x1012 },
   { VER_82546EB_A1, 0x8086, 0x101D },
   { VER_82546GB_B0, 0x8086, 0x1079 },
   { VER_82546GB_B0, 0x8086, 0x107A },
   { VER_82546GB_B0, 0x8086, 0x107B },
   { VER_82545EM_A,  0x8086, 0x100F },
   { VER_82545EM_A,  0x8086, 0x1011 },
   { VER_82545GM_B,  0x8086, 0x1026 },
   { VER_82545GM_B,  0x8086, 0x1027 },
   { VER_82545GM_B,  0x8086, 0x1028 },
   { VER_82544EI_A4, 0x8086, 0x1107 },
   { VER_82544GC_A4, 0x8086, 0x1112 },
   { VER_82541EI_A0, 0x8086, 0x1013 },
   { VER_82541EI_B0, 0x8086, 0x1013 },
   { VER_82541EI_B0, 0x8086, 0x1018 },
   { VER_82541GI_B1, 0x8086, 0x1076 },
   { VER_82541GI_B1, 0x8086, 0x1077 },
   { VER_82541PI_C0, 0x8086, 0x1076 },
   { VER_82541ER_C0, 0x8086, 0x1078 },
   { VER_82540EP_A , 0x8086, 0x1017 },
   { VER_82540EP_A , 0x8086, 0x1016 },
   { VER_82540EM_A , 0x8086, 0x100E },
   { VER_82540EM_A , 0x8086, 0x1015 },
   { -1, -1, -1 },
};

static u32 read_reg(u32 off)
{
   if (is_mmio)
      return mmio_read32(io_addr + (off >> 2));
   else {
      outl(io_addr + 0x00, off);
      return inl(io_addr + 0x4);
   }
}

static void write_reg(u32 off, u32 val)
{
   if (is_mmio)
      mmio_write32(val, io_addr + (off >> 2));
   else {
      outl(io_addr + 0x0, off);
      outl(io_addr + 0x4, val);
   }
}

static void
unused_tx_queue_slots(void)
{
   const u32 head = read_reg(REG_TDH);
   const u32 used = (tx_tail - head + TX_RING_CAP) % TX_RING_CAP;
   const u32 free = TX_RING_CAP - used - 1;
   return free;
}

/*
 * Send packet
 */
static int e1000_send(char *src, u32 len)
{
   const u32 num_desc;

   trace_printk(10, "e1000: Sending frame len=%d\n", len);

   num_desc = DIV_ROUND_UP(len, TX_BUF_SIZE);
   if (num_desc > unused_tx_queue_slots())
      return -ENOMEM;

   for (u32 i = 0, off = 0; i < num_desc; i++) {
      u32 num;

      num = MIN((u32) TX_BUF_SIZE, (u32) len - off);

      memcpy(PA_TO_KERNEL_VA(tx_ring[tx_tail].addr), src + off, num);

      tx_ring[tx_tail].length = num;
      tx_ring[tx_tail].command = TX_DESC_CMD_RS;
      if (i == num_desc-1)
         tx_ring[tx_tail].command |= TX_DESC_CMD_IFCS | TX_DESC_CMD_EOP;

      tx_tail = (tx_tail + 1) % TX_RING_CAP;
      off += num;
   }

   write_reg(REG_TDT, tx_tail);
   return 0;
}

static void process_incoming_desc(void *ctx)
{
   /*
    * Skip running the function entirely if this was a spurious
    * call and there is nothing to do
    */
   if (!(rx_ring[rx_tail].status & BIT_RX_STATUS_DD))
      return;

   do {
      if (rx_ring[rx_tail].status & BIT_RX_STATUS_EOP) {

         /*
          * Packet received in a single entry
          */

         net_process_packet(PA_TO_KERNEL_VA(rx_ring[rx_tail].addr),
                            rx_ring[rx_tail].length);
      } else {

         /*
          * Slow path. Packet spans multiple entries.
          */

         NOT_IMPLEMENTED();
      }

      rx_ring[rx_tail].status = 0;
      rx_tail = (rx_tail + 1) % RX_RING_CAP;
   } while (rx_ring[rx_tail].status & BIT_RX_STATUS_DD);

   write_reg(REG_RDT, (rx_tail + RX_RING_CAP - 1) % RX_RING_CAP);
}

static void
process_link_status_change(void *ctx)
{
    // TODO
}

static enum irq_action
irq_handler_func(void *ctx)
{
   const u32 icr;
   enum irq_action ret = IRQ_NOT_HANDLED;

   /*
    * Read the Interrupt Cause Register (ICR) and
    * determine if the interrupt came from this
    * device and, if so, the reason why it was
    * generated.
    */
   icr = read_reg(REG_ICR);

   trace_printk(9, "e1000: Interrupt!\n");

   if (icr & BIT_IMS_RXT0) {
      // Packets received
      if (!wth_enqueue_on(wth, process_incoming_desc, NULL))
         printk("e1000: WARNING: hit job queue limit\n");
      ret = IRQ_HANDLED;
   }

   if (icr & BIT_IMS_LSC) {
      // Link status change
      if (!wth_enqueue_on(wth, process_link_status_change, NULL))
         printk("e1000: WARNING: hit job queue limit\n");
      ret = IRQ_HANDLED;
   }

   if (icr & BIT_IMS_TXDW) {
      printk("e1000: TX writeback interrupt\n");
      ret = IRQ_HANDLED;
   }

   return ret;
}

DEFINE_IRQ_HANDLER_NODE(irq_handler_node, irq_handler_func, NULL);

static void reset_nic(void)
{
   u32 ctrl;

   ctrl = read_reg(REG_CTRL);
   ctrl |= BIT_CTRL_RST;
   write_reg(REG_CTRL, ctrl);

   while ((ctrl = read_reg(REG_CTRL)) & BIT_CTRL_RST)
      halt();

   ctrl |= BIT_CTRL_ASDE;
   ctrl |= BIT_CTRL_SLU;
   ctrl &= ~BIT_CTRL_LRST;
   ctrl &= ~BIT_CTRL_PHY_RST;

   write_reg(REG_CTRL, ctrl);
}

static int setup_tx_ring(void)
{
   ulong ring_paddr;
   ulong data_paddr;

   if (!(tx_ring = kzmalloc(TX_RING_PAGES * PAGE_SIZE)))
      return -ENOMEM;

   if (!(tx_data = kzmalloc(TX_DATA_PAGES * PAGE_SIZE))) {
      kfree(tx_ring);
      return -ENOMEM;
   }

   ring_paddr = KERNEL_VA_TO_PA(tx_ring);
   data_paddr = KERNEL_VA_TO_PA(tx_data);

   for (int i = 0; i < TX_RING_CAP; i++)
      tx_ring[i].addr = data_paddr + TX_BUF_SIZE * i;

   write_reg(REG_TDBAL, (u64) ring_paddr & 0xFFFFFFFF); /* queue address */
   write_reg(REG_TDBAH, (u64) ring_paddr >> 32);        /* ... */

   write_reg(REG_TDLEN, sizeof(struct tx_desc) * TX_RING_CAP); /* queue size */

   write_reg(REG_TDH, 0); /* head */
   write_reg(REG_TDT, 0); /* tail */

   write_reg(REG_TCTL, BIT_TCTL_EN | BIT_TCTL_PSP);     /* transmit mode */
   return 0;
}

static int setup_rx_ring(void)
{
   ulong ring_paddr;
   ulong data_paddr;
   u32   buf_size;
   bool  buf_size_ext;
   u32   rctl;

   if (!(rx_ring = kzmalloc(RX_RING_PAGES * PAGE_SIZE)))
      return -ENOMEM;

   if (!(rx_data = kzmalloc(RX_DATA_PAGES * PAGE_SIZE))) {
      kfree(rx_ring);
      return -ENOMEM;
   }

   ring_paddr = KERNEL_VA_TO_PA(rx_ring);
   data_paddr = KERNEL_VA_TO_PA(rx_data);

   for (int i = 0; i < RX_RING_CAP; i++)
      rx_ring[i].addr = data_paddr + RX_BUF_SIZE * i;

   write_reg(REG_RDBAL, (u64) ring_paddr & 0xFFFFFFFF); /* queue address */
   write_reg(REG_RDBAH, (u64) ring_paddr >> 32);        /* ... */

   write_reg(REG_RDLEN, sizeof(struct rx_desc) * RX_RING_CAP); /* queue size */

   write_reg(REG_RDH, 0);             /* head */
   write_reg(REG_RDT, RX_RING_CAP-1); /* tail */

   switch (RX_BUF_SIZE) {
      case 256  : buf_size = 3; buf_size_ext = 0; break;
      case 512  : buf_size = 2; buf_size_ext = 0; break;
      case 1024 : buf_size = 1; buf_size_ext = 0; break;
      case 2048 : buf_size = 0; buf_size_ext = 0; break;
      case 4096 : buf_size = 3; buf_size_ext = 1; break;
      case 8192 : buf_size = 2; buf_size_ext = 1; break;
      case 16384: buf_size = 1; buf_size_ext = 1; break;
      default: NOT_REACHED();
   }

   rctl = BIT_RCTL_EN | BIT_RCTL_BAM;
   if (buf_size_ext)
      rctl |= BIT_RCTL_BSEX;
   rctl |= buf_size << 16;
   rctl |= BIT_RCTL_UPE | BIT_RCTL_MPE; /* promiscuous mode */
   write_reg(REG_RCTL, rctl); /* receive mode */
   return 0;
}

static void enable_nic_interrupts(void)
{
   read_reg(REG_ICR);
   write_reg(REG_IMS, BIT_IMS_RXT0 | BIT_IMS_RXO | BIT_IMS_LSC | BIT_IMS_TXDW);
}

static void eeprom_unlock(void)
{
   u32 eecd;

   eecd = read_reg(REG_EECD);
   eecd &= ~BIT_EECD_EE_REQ;
   write_reg(REG_EECD, eecd);
}

static int eeprom_lock(void)
{
   u32 eecd;

   eecd = read_reg(REG_EECD);

   if (!(eecd & BIT_EECD_EE_PRES))
      return -ENODEV; /* No EEPROM found */

   eecd |= BIT_EECD_EE_REQ;
   write_reg(REG_EECD, eecd);

   while (!(read_reg(REG_EECD) & BIT_EECD_EE_GNT))
      halt();

   return 0;
}

static void
eeprom_read_nolock(u8 off, u16 *dst)
{
   u32 eerd;

   if (device_version == VER_82547EI_A0 ||
       device_version == VER_82547EI_A1 ||
       device_version == VER_82547EI_B0 ||
       device_version == VER_82547GI_B0 ||
       device_version == VER_82541EI_A0 ||
       device_version == VER_82541EI_B0 ||
       device_version == VER_82541ER_C0 ||
       device_version == VER_82541GI_B1 ||
       device_version == VER_82541PI_C0)
      eerd = (off & 0xFFF) << 2;
   else
      eerd = (off & 0xFF) << 8;

   eerd |= BIT_EERD_START;
   write_reg(REG_EERD, eerd);

   while (!((eerd = read_reg(REG_EERD)) & BIT_EERD_DONE))
      halt();

   *dst = eerd >> 16;
}

static int eeprom_read(u8 off, u16 *dst)
{
   int rc;

   rc = eeprom_lock();
   if (rc)
      return rc;

   eeprom_read_nolock(off, dst);

   eeprom_unlock();
   return 0;
}

static int
load_mac_addr(void)
{
   int rc;
   u16 b0, b1, b2;
   u32 lo, hi;

   rc = eeprom_read(0, &b0);
   if (rc)
      return rc;

   rc = eeprom_read(1, &b1);
   if (rc)
      return rc;

   rc = eeprom_read(2, &b2);
   if (rc)
      return rc;

   mac.data[0] = b0 & 0xFF;
   mac.data[1] = b0 >> 8;
   mac.data[2] = b1 & 0xFF;
   mac.data[3] = b1 >> 8;
   mac.data[4] = b2 & 0xFF;
   mac.data[5] = b2 >> 8;
   printk("e1000: MAC address is %x:%x:%x:%x:%x:%x\n",
       mac.data[0], mac.data[1], mac.data[2],
       mac.data[3], mac.data[4], mac.data[5]);

   lo = ((u32) b1 << 16) | b0;
   hi = b2;

   write_reg(REG_RAL, lo);
   write_reg(REG_RAH, hi | (1U << 31));
   return 0;
}

/*
 * Now determine how much memory the NIC wants us to map
 * by writing 0xFFFFFFFF to BAR0 and seeing how many bits
 * it keeps up.
 *
 * Returns 0 if the PCI config space couldn't be read or
 * written.
 */
static size_t
determine_bar0_addr_space(struct pci_device_loc loc)
{
   int rc;
   u32 bar0;
   u32 old_bar0;

   rc = pci_config_read(loc, PCI_REG_BAR0, 8*sizeof(old_bar0), &old_bar0);
   if (rc)
      return 0;

   /* Write all 1s */
   bar0 = 0xFFFFFFFF;
   rc = pci_config_write(loc, PCI_REG_BAR0, 8*sizeof(bar0), bar0);
   if (rc)
      return 0;

   /* Read it back to see how many we managed to set */
   rc = pci_config_read(loc, PCI_REG_BAR0, 8*sizeof(bar0), &bar0);
   if (rc)
      return 0;

   /* Translate the value read to a number of pages */
   bar0 &= ~0xF;
   const u32 num_pages = DIV_ROUND_UP(1 + ~bar0, PAGE_SIZE);

   /* Restore the previous value */
   rc = pci_config_write(loc, PCI_REG_BAR0, 8*sizeof(old_bar0), old_bar0);
   if (rc)
      return 0;

   return num_pages;
}

/*
 * Reads the NIC's physical address (from BAR0/BAR1) and
 * maps it to a virtual address
 */
static int read_io_addr(struct pci_device_loc loc)
{
   int rc;
   u32 bar0;
   u64 paddr;
   u32 type;

   rc = pci_config_read(loc, PCI_REG_BAR0, 8*sizeof(bar0), &bar0);
   if (rc) {
      printk("e1000: ERROR: Unable to read BAR0\n");
      return rc;
   }

   is_mmio = !(bar0 & 1);
   paddr = bar0 & ~0xF;

   type = (bar0 >> 1) & 3;
   if (type == 2) {
      u32 bar1;

      rc = pci_config_read(loc, PCI_REG_BAR1, 8*sizeof(bar1), &bar1);
      if (rc) {
         printk("e1000: ERROR: Unable to read BAR1\n");
         return rc;
      }

      paddr |= ((u64) bar1 << 32);
   }

   if (is_mmio) {
      size_t num_pages;
      void *vaddr;

      num_pages = determine_bar0_addr_space(loc);
      if (num_pages == 0) {
         printk("e1000: ERROR: Unable to determine NIC address space size\n");
         return -EINVAL;
      }

      vaddr = hi_vmem_reserve(num_pages * PAGE_SIZE);
      if (!vaddr) {
         printk("e1000: ERROR: Unable to reserve virtual memory\n");
         return -ENOMEM;
      }

      if (map_kernel_pages(vaddr, paddr, num_pages,
          PAGING_FL_RW | PAGING_FL_CD) != num_pages) {

         printk("e1000: ERROR: Unable to map physical memory\n");
         hi_vmem_release(vaddr, num_pages * PAGE_SIZE);
         return -ENOMEM;
      }

      io_addr = (ulong) vaddr;

   } else {
      io_addr = paddr;
   }
   return 0;
}

static int
read_pci_interrupt_line(struct pci_device_loc loc)
{
   int rc;
   u32 interrupt_line;

   rc = pci_config_read(loc, PCI_REG_INTR_LINE, 8, &interrupt_line);
   if (rc < 0)
      return rc;

   ASSERT(interrupt_line < INT_MAX);
   return (int) interrupt_line;
}

static int
configure_pci_command_reg(struct pci_device_loc loc)
{
   int rc;
   u32 cmd;

   rc = pci_config_read(loc, PCI_REG_CMD, 16, &cmd);
   if (rc)
      return rc;

   cmd |= BIT_PCI_REG_CMD_BME;
   cmd &= ~BIT_PCI_REG_CMD_CID;

   rc = pci_config_write(loc, PCI_REG_CMD, 16, cmd);
   if (rc)
      return rc;

   return 0;
}

static struct mac_addr e1000_get_mac_addr(void)
{
   return mac;
}

static struct pci_device*
find_compatible_pci_device(int *version)
{
   for (int i = 0; device_table[i].version > -1; i++) {
      struct pci_device *dev;

      dev = pci_get_object_by_id(device_table[i].vendor_id,
                                 device_table[i].device_id);
      if (dev) {
         *version = device_table[i].version;
         return dev;
      }
   }

   return NULL;
}

static void
init_e1000(void)
{
   int rc;
   struct pci_device *dev;
   u8 interrupt_line;

   dev = find_compatible_device(&device_version);
   if (!dev) {
      printk("e1000: INFO: No compatible device found\n");
      return; /* No matching device found */
   }

   printk("e1000: INFO: Found device (vendor_id=%x, device_id=%x)\n",
          dev->vendor_id, dev->device_id);

   rc = read_pci_interrupt_line(dev->loc);
   if (rc < 0) {
      printk("e1000: INFO: Couldn't read interrupt line from "
             "PCI config register\n");
      return;
   }
   interrupt_line = (u8) rc;

   rc = configure_pci_command_reg(dev->loc);
   if (rc) {
      printk("e1000: INFO: Couldn't configure PCI command register\n");
      return;
   }

   rc = read_io_addr(dev->loc);
   if (rc) {
      printk("e1000: ERROR: unable to map NIC memory\n");
      return;
   }

   reset_nic();

   rc = load_mac_addr();
   if (rc) {
      printk("e1000: ERROR: unable to load MAC address\n");
      return;
   }

   rc = setup_tx_ring();
   if (rc) {
      printk("e1000: ERROR: unable to setup TX ring\n");
      return;
   }

   rc = setup_rx_ring();
   if (rc) {
      printk("e1000: ERROR: unable to setup RX ring\n");
      return;
   }

   disable_preemption();
   {
      int prio = 1; /* ??? */
      int qsize = 8; /* TODO: make this configurable */
      wth = wth_create_thread("e1000", prio, qsize);
   }
   enable_preemption();

   if (!wth) {
      printk("e1000: Unable to create a worker thread for IRQs");
      return;
   }

   net_driver_funcs.get_mac_addr = e1000_get_mac_addr;
   net_driver_funcs.send_frame = e1000_send;

   irq_install_handler(interrupt_line, &irq_handler_node);

   enable_nic_interrupts();
   printk("e1000: INFO: enabled NIC interrupts\n");
}

static struct module e1000_module = {
   .name = "e1000",
   .priority = MOD_e1000_prio,
   .init = &init_e1000,
};

REGISTER_MODULE(&e1000_module);
