/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/boot.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/modules.h>
#include <tilck/kernel/hal.h>
#include <3rd_party/fdt_helper.h>
#include <libfdt.h>
#include <tilck/mods/irqchip.h>
#include "fdt_serial.h"

/*
 * Definitions from:
 * https://en.wikibooks.org/wiki/Serial_Programming/8250_UART_Programming
 */

#define UART_THR         0   /* [ W] Transmitter Holding Buffer */
#define UART_RBR         0   /* [R ] Receiver Buffer */
#define UART_DLL         0   /* [RW] DLAB: Divisor Latch Low Byte */
#define UART_IER         1   /* [RW] Interrupt Enable Register */
#define UART_DLH         1   /* [RW] DLAB: Divisor Latch High Byte */
#define UART_IIR         2   /* [R ] Interrupt Identification Register */
#define UART_FCR         2   /* [ W] FIFO Control Register */
#define UART_LCR         3   /* [RW] Line Control Register */
#define UART_MCR         4   /* [RW] Modem Control Register */
#define UART_LSR         5   /* [R ] Line Status Register */
#define UART_MSR         6   /* [R ] Modem Status Register */
#define UART_SR          7   /* [RW] Scratch Register */

/* Interrupt Enable Register (IER) */
#define IER_NO_INTR                0b00000000
#define IER_RCV_AVAIL_INTR         0b00000001
#define IER_TR_EMPTY_INTR          0b00000010
#define IER_RCV_LINE_STAT_INTR     0b00000100
#define IER_MOD_STAT_INTR          0b00001000
#define IER_SLEEP_MODE_INTR        0b00010000
#define IER_LOW_PWR_INTR           0b00100000

/* Line Status Register (LSR) */
#define LSR_DATA_READY             0b00000001
#define LSR_OVERRUN_ERROR          0b00000010
#define LSR_PARITY_ERROR           0b00000100
#define LSR_FRAMING_ERROR          0b00001000
#define LSR_BREAK_INTERRUPT        0b00010000
#define LSR_EMPTY_TR_REG           0b00100000
#define LSR_EMPTY_DATA_HOLD_REG    0b01000000
#define LSR_ERROR_RECV_FIFO        0b10000000

/* Line Control Register (LCR) */

                           /* bits:        xx */
#define LCR_5_BITS                 0b00000000
#define LCR_6_BITS                 0b00000001
#define LCR_7_BITS                 0b00000010
#define LCR_8_BITS                 0b00000011

                           /* bits:       x  */
#define LCR_1_STOP_BIT             0b00000000
#define LCR_1_5_OR_2_STOP_BITS     0b00000100

                           /* bits:    xxx   */
#define LCR_NO_PARITY              0b00000000
#define LCR_ODD_PARITY             0b00001000
#define LCR_EVEN_PARITY            0b00011000
#define LCR_MARK_PARITY            0b00101000
#define LCR_SPACE_PARITY           0b00111000

#define LCR_SET_BREAK_ENABLE       0b01000000
#define LCR_SET_DIV_LATCH_ACC_BIT  0b10000000

/* FIFO Control Register (FCR) */
#define FCR_ENABLE_FIFOs           0b00000001
#define FCR_CLEAR_RECV_FIFO        0b00000010
#define FCR_CLEAR_TR_FIFO          0b00000100
#define FCR_DMA_MODE               0b00001000
#define FCR_RESERVED               0b00010000 /* DO NOT USE */
#define FCR_64_BYTE_FIFO           0b00100000
#define FCR_INT_TRIG_LEVEL_0       0b00000000 /* 1 byte (16 and 64 byte FIFO) */
#define FCR_INT_TRIG_LEVEL_1       0b01000000 /* 4 / 16 bytes (64 byte FIFO)  */
#define FCR_INT_TRIG_LEVEL_2       0b10000000 /* 8 / 32 bytes (64 byte FIFO)  */
#define FCR_INT_TRIG_LEVEL_3       0b11000000 /* 14 / 56 bytes (64 byte FIFO) */

/* Modem Control Register (MCR) */
#define MCR_DTR                    0b00000001 /* Data Terminal Ready */
#define MCR_RTS                    0b00000010 /* Request To Send */
#define MCR_AUX_OUTPUT_1           0b00000100
#define MCR_AUX_OUTPUT_2           0b00001000 /* Necessary for interrupts to
                                                 for most of the controllers */
#define MCR_LOOPBACK_MODE          0b00010000
#define MCR_AUTOFLOW_CTRL          0b00100000 /* Only on 16750 */
#define MCR_RESERVED_1             0b01000000
#define MCR_RESERVED_2             0b10000000

/* Modem Status Register (MSR) */
#define MSR_DELTA_CTS              0b00000001 /* Delta Clear To Send */
#define MSR_DELTA_DSR              0b00000010 /* Delta Data Set Ready */
#define MSR_TERI                   0b00000100 /* Trailing Edge Ring Indicator */
#define MSR_DELTA_DCD              0b00001000 /* Delta Data Carrier Detect */
#define MSR_CTS                    0b00010000 /* Clear To Send */
#define MSR_DSR                    0b00100000 /* Data Set Ready */
#define MSR_RI                     0b01000000 /* Ring Indicator */
#define MSR_CD                     0b10000000 /* Carrier Detect */

struct ns16550 {
   void *base;
   ulong paddr;
   ulong size;
   int irq;

   ulong uartclk;
   int reg_width;
   int reg_shift;
   int reg_offset;
   struct clock *clk;
   struct clock *pclk;
};

static void ns16550_reg_wr(struct ns16550 *uart, int offset, int value)
{
   unsigned char *addr;

   offset *= 1 << uart->reg_shift;
   addr = (unsigned char *)uart->base + offset + uart->reg_offset;

   if (uart->reg_width == 4)
      mmio_writel(value, addr);
   else
      mmio_writeb(value, addr);
}

static int ns16550_reg_rd(struct ns16550 *uart, int offset)
{
   unsigned char *addr;

   offset *= 1 << uart->reg_shift;
   addr = (unsigned char *)uart->base + offset + uart->reg_offset;

   if (uart->reg_width == 4)
      return mmio_readl(addr);
   else
      return mmio_readb(addr);
}

static bool ns16550_read_ready(void *priv)
{
   struct ns16550 *uart = priv;
   return !!(ns16550_reg_rd(uart, UART_LSR) & LSR_DATA_READY);
}

static void ns16550_wait_for_read(void *priv)
{
   struct ns16550 *uart = priv;
   while (!ns16550_read_ready(uart)) { }
}

static char ns16550_read(void *priv)
{
   struct ns16550 *uart = priv;
   ns16550_wait_for_read(uart);
   return (char) ns16550_reg_rd(uart, UART_RBR);
}

static bool ns16550_write_ready(void *priv)
{
   struct ns16550 *uart = priv;
   return !!(ns16550_reg_rd(uart, UART_LSR) & LSR_EMPTY_TR_REG);
}

static void ns16550_wait_for_write(void *priv)
{
   struct ns16550 *uart = priv;
   while (!ns16550_write_ready(uart)) { }
}

static void ns16550_write(void *priv, char c)
{
   struct ns16550 *uart = priv;
   ns16550_wait_for_write(uart);
   ns16550_reg_wr(uart, UART_THR,(u8)c);
}

static void ns16550_set_baud_divisor(struct ns16550 *uart, int baud_divisor)
{
   int lcr_val = ns16550_reg_rd(uart, UART_LCR) & ~LCR_SET_DIV_LATCH_ACC_BIT;

   ns16550_reg_wr(uart, UART_LCR, LCR_SET_DIV_LATCH_ACC_BIT | lcr_val);
   ns16550_reg_wr(uart, UART_DLL, baud_divisor & 0xff);
   ns16550_reg_wr(uart, UART_DLH, (baud_divisor >> 8) & 0xff);
   ns16550_reg_wr(uart, UART_LCR, lcr_val);
}

static void ns16550_uart_init(struct ns16550 *uart, int baud_divisor)
{
   ns16550_reg_wr(uart, UART_IER, IER_NO_INTR);
   ns16550_reg_wr(uart, UART_MCR, MCR_DTR | MCR_RTS);
   ns16550_reg_wr(uart, UART_FCR, FCR_ENABLE_FIFOs |
                                 FCR_CLEAR_RECV_FIFO |
                                 FCR_CLEAR_TR_FIFO);

   ns16550_reg_wr(uart, UART_LCR, LCR_8_BITS | LCR_1_STOP_BIT | LCR_NO_PARITY);

   ns16550_set_baud_divisor(uart, baud_divisor);

   ns16550_reg_wr(uart, UART_IER, IER_RCV_AVAIL_INTR);
}

static int
fdt_parse_ns16550(void *fdt, int node, struct ns16550 *uart)
{
   int len, rc;
   const fdt32_t *val;
   u64 addr, size;

   if (node < 0 || !uart || !fdt)
      return -ENODEV;

   rc = fdt_get_node_addr_size(fdt, node, 0, &addr, &size);
   if (rc < 0 || !addr || !size)
      return -ENODEV;

   uart->paddr = addr;
   uart->size = size;

   /* set default */
   uart->reg_width = 1;
   uart->reg_shift = 0;
   uart->reg_offset = 0;
   uart->uartclk = 0;

   val = fdt_getprop(fdt, node, "reg-io-width", &len);
   if (val)
      uart->reg_width = fdt32_to_cpu(*val);

   val = fdt_getprop(fdt, node, "reg-shift", &len);
   if (val)
      uart->reg_shift = fdt32_to_cpu(*val);

   val = fdt_getprop(fdt, node, "reg-offset", &len);
   if (val)
      uart->reg_offset = fdt32_to_cpu(*val);

   val = fdt_getprop(fdt, node, "clock-frequency", &len);
   if (val)
      uart->uartclk = fdt32_to_cpu(*val);

   return 0;
}

struct fdt_serial_ops ns16550_ops = {
   .rx_rdy = ns16550_read_ready,
   .rx_c = ns16550_read,
   .tx_c = ns16550_write,
};

int ns16550_init(void *fdt, int node, const struct fdt_match *match)
{
   struct ns16550 *uart;
   int rc;

   uart = kzalloc_obj(struct ns16550);
   if (!uart) {
      printk("ns16550: ERROR: nomem!\n");
      return -ENOMEM;
   }

   rc = fdt_parse_ns16550(fdt, node, uart);
   if (rc)
      goto bad;

   uart->base = ioremap(uart->paddr, uart->size);
   if (!uart->base) {
      printk("ns16550: ERROR: ioremap failed for %p\n", (void *)uart->paddr);
      rc = -EIO;
      goto bad;
   }

   /*
    * TODO: Since tilck does not currently implement the "clock" framework,
    * we must specify the clock frequency at the uart node of the device tree
    */
   if (!uart->uartclk) {
      printk("ns16550: ERROR: UART frequency undefined!\n");
      uart->uartclk = 384000; //try default frequency
   }

   uart->irq = irqchip_alloc_irq(fdt, node, 0);
   if (uart->irq < 0) {
      printk("ns16550: ERROR: cannot alloc globle irq number!\n");
      rc = -EINVAL;
      goto bad;
   }

   fdt_serial_register(uart, &ns16550_ops, uart->irq,
                       fdt_serial_generic_irq_handler);

   ns16550_uart_init(uart, uart->uartclk / (16 * 115200));

   return 0;

bad:
   kfree(uart);
   return rc;
}

static const struct fdt_match ns16550_ids[] = {
   {.compatible = "snps,dw-apb-uart"},
   {.compatible = "ns16550a"},
   {.compatible = "ns16550"},
   { }
};

REGISTER_FDT_SERIAL(ns16550, ns16550_ids, ns16550_init)

