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
 * Registers & Flags
 */

#define PCI_REG_INTR_LINE 0x3C
#define PCI_REG_CMD       0x04
#define PCI_REG_BAR0      0x10
#define PCI_REG_BAR1      0x14

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

#define BIT_PCI_REG_CMD_BME (1 << 2) /* Bus Master Enable */
#define BIT_PCI_REG_CMD_CID (1 << 10) /* Clear Interrupt Disable */

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
 * Transmission descriptor command flags
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
 * Configuration
 */

#define TX_DESC_SIZE 16
#define TX_RING_CAP 32
#define TX_BUF_SIZE 512 // Not sure?

#define RX_DESC_SIZE 16
#define RX_RING_CAP 32

/*
 * Receive buffer size. Must be a power of two
 * between 256 and 16384 (inclusive).
 *
 * We go with 2K as ethernet frames are around
 * 1.5K bytes.
 */
#define RX_BUF_SIZE 2048

/*
 * Module state
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

STATIC_ASSERT(sizeof(struct tx_desc) == TX_DESC_SIZE);
STATIC_ASSERT(sizeof(struct rx_desc) == RX_DESC_SIZE);

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
    if (is_mmio) {
        return mmio_read32(io_addr, off);
    } else {
        outl(io_addr + 0x00, off);
        return inl(io_addr + 0x4);
    }
}

static void write_reg(u32 off, u32 val)
{
    if (is_mmio) {
        mmio_write32(io_addr, off, val);
    } else {
        outl(io_addr + 0x0, off);
        outl(io_addr + 0x4, val);
    }
}

/*
 * Send packet
 */
static int e1000_send(char *src, u32 len)
{
    trace_printk(10, "e1000: Sending frame len=%d\n", len);

    /* Number of descriptors that would be required to send this message. */
    u32 num_desc = DIV_ROUND_UP(len, TX_BUF_SIZE);

    u32 head = read_reg(REG_TDH);
    u32 used = (tx_tail - head + TX_RING_CAP) % TX_RING_CAP;
    u32 free = TX_RING_CAP - used - 1;

    if (num_desc > free)
        return -ENOMEM;

    for (u32 i = 0, off = 0; i < num_desc; i++) {

        u32 num = MIN((u32) TX_BUF_SIZE, (u32) len - off);

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

            void  *src = PA_TO_KERNEL_VA(rx_ring[rx_tail].addr);
            u32    len = rx_ring[rx_tail].length;

            net_process_packet(src, len);
        } else {
            /*
            * Slow path. Packet spans multiple entries. Not implemented yet.
            */
            NOT_IMPLEMENTED();
        }

        rx_ring[rx_tail].status = 0;
        rx_tail = (rx_tail + 1) % RX_RING_CAP;
    } while (rx_ring[rx_tail].status & BIT_RX_STATUS_DD);

    write_reg(REG_RDT, (rx_tail + RX_RING_CAP - 1) % RX_RING_CAP);
}

static void process_link_status_change(void *ctx)
{
    // TODO
}

static enum irq_action irq_handler_func(void *ctx)
{
    enum irq_action ret = IRQ_NOT_HANDLED;

    /*
     * Read the Interrupt Cause Register (ICR) and
     * determine if the interrupt came from this
     * device and, if so, the reason why it was
     * generated.
     */
    u32 icr = read_reg(REG_ICR);

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

    // TODO: What if IMS_RXO is received?

    return ret;
}

DEFINE_IRQ_HANDLER_NODE(irq_handler_node, irq_handler_func, NULL);

static void reset_nic(void)
{
    u32 ctrl = read_reg(REG_CTRL);
    ctrl |= BIT_CTRL_RST;
    write_reg(REG_CTRL, ctrl);

    while ((ctrl = read_reg(REG_CTRL)) & BIT_CTRL_RST) {
        __asm__ ("hlt");
    }

    ctrl |= BIT_CTRL_ASDE;
    ctrl |= BIT_CTRL_SLU;
    ctrl &= ~BIT_CTRL_LRST;
    ctrl &= ~BIT_CTRL_PHY_RST;

    write_reg(REG_CTRL, ctrl);
}

static int setup_tx_ring(void)
{
    size_t ring_bytes = TX_DESC_SIZE * TX_RING_CAP;
    size_t data_bytes = TX_BUF_SIZE * TX_RING_CAP;

    size_t ring_pages = DIV_ROUND_UP(ring_bytes, PAGE_SIZE);
    size_t data_pages = DIV_ROUND_UP(data_bytes, PAGE_SIZE);

    tx_ring = kzmalloc(ring_pages * PAGE_SIZE);
    if (tx_ring == NULL)
        return -ENOMEM;
    ulong ring_paddr = KERNEL_VA_TO_PA(tx_ring);

    tx_data = kzmalloc(data_pages * PAGE_SIZE);
    if (tx_data == NULL) {
        kfree(tx_ring);
        return -ENOMEM;
    }
    ulong data_paddr = KERNEL_VA_TO_PA(tx_data);

    for (int i = 0; i < TX_RING_CAP; i++) {
        tx_ring[i].addr = data_paddr + TX_BUF_SIZE * i;
    }

    write_reg(REG_TDBAL, (u64) ring_paddr & 0xFFFFFFFF); /* queue address (low) */
    write_reg(REG_TDBAH, (u64) ring_paddr >> 32);        /* queue address (high) */
    write_reg(REG_TDLEN, TX_DESC_SIZE * TX_RING_CAP);    /* queue size in bytes */
    write_reg(REG_TDH, 0);                               /* head */
    write_reg(REG_TDT, 0);                               /* tail */

    write_reg(REG_TCTL, BIT_TCTL_EN | BIT_TCTL_PSP);     /* transmit mode */
    return 0;
}

static int setup_rx_ring(void)
{
    size_t ring_bytes = RX_DESC_SIZE * RX_RING_CAP;
    size_t data_bytes = RX_BUF_SIZE * RX_RING_CAP;

    size_t ring_pages = DIV_ROUND_UP(ring_bytes, PAGE_SIZE);
    size_t data_pages = DIV_ROUND_UP(data_bytes, PAGE_SIZE);

    rx_ring = kzmalloc(ring_pages * PAGE_SIZE);
    if (rx_ring == NULL)
        return -ENOMEM;
    ulong ring_paddr = KERNEL_VA_TO_PA(rx_ring);

    rx_data = kzmalloc(data_pages * PAGE_SIZE);
    if (rx_data == NULL) {
        kfree(rx_ring);
        return -ENOMEM;
    }
    ulong data_paddr = KERNEL_VA_TO_PA(rx_data);

    for (int i = 0; i < RX_RING_CAP; i++) {
        rx_ring[i].addr = data_paddr + RX_BUF_SIZE * i;
    }

    write_reg(REG_RDBAL, (u64) ring_paddr & 0xFFFFFFFF); /* queue address (low) */
    write_reg(REG_RDBAH, (u64) ring_paddr >> 32);        /* queue address (high) */
    write_reg(REG_RDLEN, RX_DESC_SIZE * RX_RING_CAP);    /* queue size in bytes */
    write_reg(REG_RDH, 0);                               /* head */
    write_reg(REG_RDT, RX_RING_CAP-1);                   /* tail */

    u32 bsize;
    bool bsex;
    switch (RX_BUF_SIZE) {
    case 256  : bsize = 3; bsex = 0; break;
    case 512  : bsize = 2; bsex = 0; break;
    case 1024 : bsize = 1; bsex = 0; break;
    case 2048 : bsize = 0; bsex = 0; break;
    case 4096 : bsize = 3; bsex = 1; break;
    case 8192 : bsize = 2; bsex = 1; break;
    case 16384: bsize = 1; bsex = 1; break;
    default: NOT_REACHED();
    }

    u32 rctl = BIT_RCTL_EN | BIT_RCTL_BAM;

    if (bsex)
        rctl |= BIT_RCTL_BSEX;
    rctl |= bsize << 16;

    rctl |= BIT_RCTL_UPE | BIT_RCTL_MPE; /* promiscuous mode */

    write_reg(REG_RCTL, rctl); /* receive mode */
    return 0;
}

static void enable_nic_interrupts(void)
{
    read_reg(REG_ICR);
    //write_reg(REG_IMS, BIT_IMS_RXT0 | BIT_IMS_RXO | BIT_IMS_LSC);
    write_reg(REG_IMS, BIT_IMS_RXT0 | BIT_IMS_RXO | BIT_IMS_LSC | BIT_IMS_TXDW);
}

static void eeprom_unlock(void)
{
    u32 eecd = read_reg(REG_EECD);
    eecd &= ~BIT_EECD_EE_REQ;
    write_reg(REG_EECD, eecd);
}

static int eeprom_lock(void)
{
    u32 eecd = read_reg(REG_EECD);

    if (!(eecd & BIT_EECD_EE_PRES))
        return -ENODEV; /* No EEPROM found */

    eecd |= BIT_EECD_EE_REQ;
    write_reg(REG_EECD, eecd);

    while (!(read_reg(REG_EECD) & BIT_EECD_EE_GNT)) {
        __asm__ ("hlt");
    }

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
        device_version == VER_82541PI_C0) {
        eerd = (off & 0xFFF) << 2;
    } else {
        eerd = (off & 0xFF) << 8;
    }

    eerd |= BIT_EERD_START;
    write_reg(REG_EERD, eerd);

    while (!((eerd = read_reg(REG_EERD)) & BIT_EERD_DONE)) {
        __asm__ ("hlt");
    }

    *dst = eerd >> 16;
}

// TODO: is the u8 off right?
static int eeprom_read(u8 off, u16 *dst)
{
    int rc = eeprom_lock();
    if (rc) return rc;

    eeprom_read_nolock(off, dst);

    eeprom_unlock();
    return 0;
}

static int load_mac_addr(void)
{
    int rc;
    u16 b0, b1, b2;

    rc = eeprom_read(0, &b0);
    if (rc) return rc;

    rc = eeprom_read(1, &b1);
    if (rc) return rc;

    rc = eeprom_read(2, &b2);
    if (rc) return rc;

    mac.data[0] = b0 & 0xFF;
    mac.data[1] = b0 >> 8;
    mac.data[2] = b1 & 0xFF;
    mac.data[3] = b1 >> 8;
    mac.data[4] = b2 & 0xFF;
    mac.data[5] = b2 >> 8;
    printk("e1000: MAC address is %x:%x:%x:%x:%x:%x\n",
        mac.data[0], mac.data[1], mac.data[2],
        mac.data[3], mac.data[4], mac.data[5]);

    u32 lo = ((u32) b1 << 16) | b0;
    u32 hi = b2;

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
    u32 old_bar0;
    int rc = pci_config_read(loc, PCI_REG_BAR0, 8*sizeof(old_bar0), &old_bar0);
    if (rc) return 0;

    /* Write all 1s */
    u32 bar0 = 0xFFFFFFFF;
    rc = pci_config_write(loc, PCI_REG_BAR0, 8*sizeof(bar0), bar0);
    if (rc) return 0;

    /* Read it back to see how many we managed to set */
    rc = pci_config_read(loc, PCI_REG_BAR0, 8*sizeof(bar0), &bar0);
    if (rc) return 0;

    /* Translate the value read to a number of pages */
    bar0 &= ~0xF;
    u32 num_bytes = 1 + ~bar0;
    u32 num_pages = DIV_ROUND_UP(num_bytes, PAGE_SIZE);

    /* Restore the previous value */
    rc = pci_config_write(loc, PCI_REG_BAR0, 8*sizeof(old_bar0), old_bar0);
    if (rc) return 0;

    return num_pages;
}

/*
 * Reads the NIC's physical address (from BAR0/BAR1) and
 * maps it to a virtual address
 */
static int read_io_addr(struct pci_device_loc loc)
{
    u32 bar0;
    int rc = pci_config_read(loc, PCI_REG_BAR0, 8*sizeof(bar0), &bar0);
    if (rc) {
        printk("e1000: ERROR: Unable to read BAR0\n");
        return rc;
    }

    is_mmio = !(bar0 & 1);
    u64 paddr = bar0 & ~0xF;

    u32 type = (bar0 >> 1) & 3;
    if (type == 2) {

        u32 bar1;
        rc = pci_config_read(loc, PCI_REG_BAR1, 8*sizeof(bar1), &bar1);
        if (rc) {
            printk("e1000: ERROR: Unable to read BAR1\n");
            return rc;
        }

        paddr |= ((u64) bar1 << 32);

    } else {
        // TODO: The type bits must either be 0 or 2. If we don't
        //       get 0 here, the hardware is bad.
    }

    if (is_mmio) {

        printk("e1000: INFO: Using MMIO\n");

        size_t num_pages = determine_bar0_addr_space(loc);
        if (num_pages == 0) {
            printk("e1000: ERROR: Unable to determine NIC address space size\n");
            return -EINVAL;
        }
        printk("e1000: INFO: %d pages mapped to device memory\n", (int) num_pages);

        void *vaddr = hi_vmem_reserve(num_pages * PAGE_SIZE);
        if (vaddr == NULL) {
            printk("e1000: ERROR: Unable to reserve virtual memory\n");
            return -ENOMEM;
        }

        u32 page_flags = PAGING_FL_RW | PAGING_FL_CD;
        size_t num = map_kernel_pages(vaddr, paddr, num_pages, page_flags);
        if (num != num_pages) {
            printk("e1000: ERROR: Unable to map physical memory\n");
            hi_vmem_release(vaddr, num_pages * PAGE_SIZE);
            return -ENOMEM;
        }

        io_addr = (ulong) vaddr;

    } else {

        printk("e1000: INFO: Using regular I/O\n");
        io_addr = paddr;
    }
    return 0;
}

static int
read_pci_interrupt_line(struct pci_device_loc loc)
{
    u32 interrupt_line;
    int rc = pci_config_read(loc, PCI_REG_INTR_LINE, 8, &interrupt_line);
    if (rc < 0) return rc;

    ASSERT(interrupt_line < INT_MAX);
    return (int) interrupt_line;
}

static int
configure_pci_command_reg(struct pci_device_loc loc)
{
    u32 cmd;
    int rc = pci_config_read(loc, PCI_REG_CMD, 16, &cmd);
    if (rc) return rc;

    cmd |= BIT_PCI_REG_CMD_BME;
    cmd &= ~BIT_PCI_REG_CMD_CID;

    rc = pci_config_write(loc, PCI_REG_CMD, 16, cmd);
    if (rc) return rc;

    return 0;
}

static struct mac_addr e1000_get_mac_addr(void)
{
    return mac;
}

static void init_e1000(void)
{
    struct pci_device *dev = NULL;

    /* TODO: Should the deriver handle multiple devices? */
    int device_idx = 0;
    while (device_table[device_idx].version > -1) {

        u16 vendor_id = device_table[device_idx].vendor_id;
        u16 device_id = device_table[device_idx].device_id;

        dev = pci_get_object_by_id(vendor_id, device_id);

        if (dev) break;
        device_idx++;
    }
    if (dev == NULL) {
        printk("e1000: INFO: No compatible device found\n");
        return; /* No matching device found */
    }
    device_version = device_table[device_idx].version;

    printk("e1000: INFO: Found device (vendor_id=%x, device_id=%x)\n",
        device_table[device_idx].vendor_id,
        device_table[device_idx].device_id);

    int rc = read_pci_interrupt_line(dev->loc);
    if (rc < 0) {
        printk("e1000: INFO: Couldn't read interrupt line from PCI config register\n");
        return;
    }
    u8 interrupt_line = (u8) rc;

    rc = configure_pci_command_reg(dev->loc);
    if (rc) {
        printk("e1000: INFO: Couldn't configure PCI command register\n");
        return;
    }
    printk("e1000: INFO: PCI command register configured\n");

    rc = read_io_addr(dev->loc);
    if (rc) {
        printk("e1000: ERROR: unable to map NIC memory\n");
        return;
    }
    printk("e1000: INFO: Device memory mapped\n");

    reset_nic();
    printk("e1000: INFO: NIC was reset\n");

    rc = load_mac_addr();
    if (rc) {
        printk("e1000: ERROR: unable to load MAC address\n");
        return;
    }
    printk("e1000: INFO: MAC address loaded\n");

    rc = setup_tx_ring();
    if (rc) {
        printk("e1000: ERROR: unable to setup TX ring\n");
        return;
    }
    printk("e1000: INFO: TX ring set up\n");

    rc = setup_rx_ring();
    if (rc) {
        printk("e1000: ERROR: unable to setup RX ring\n");
        return;
    }
    printk("e1000: INFO: RX ring set up\n");

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

    /* Plug the driver into the network stack */
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
