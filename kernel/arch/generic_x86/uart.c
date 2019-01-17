/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/serial.h>
#include <tilck/kernel/hal.h>

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

/* Set DLAB [Divisor Latch Access Bit] to `value` */
static void uart_set_dlab(u16 port, bool value)
{
   u8 lcr = inb(port + UART_LCR);
   outb(port + UART_LCR, (lcr & 0b01111111) | (value << 7));
}

static void uart_set_divisor_latch(u16 port, u16 value)
{
   uart_set_dlab(port, 1);
   outb(port + UART_DLL, value & 0xff);
   outb(port + UART_DLH, value >> 8);
   uart_set_dlab(port, 0);
}

void init_serial_port(u16 port)
{
   outb(port + UART_IER, IER_NO_INTR);

   uart_set_divisor_latch(port, 1 /* 115200 baud */);

   outb(port + UART_LCR, LCR_8_BITS | LCR_1_STOP_BIT | LCR_NO_PARITY);

   outb(port + UART_FCR, FCR_ENABLE_FIFOs |
                         FCR_CLEAR_RECV_FIFO |
                         FCR_CLEAR_TR_FIFO |
                         FCR_INT_TRIG_LEVEL_3);

   outb(port + UART_MCR, MCR_DTR | MCR_RTS | MCR_AUX_OUTPUT_2);
   outb(port + UART_IER, IER_RCV_AVAIL_INTR);
}

bool serial_read_ready(u16 port)
{
   return !!(inb(port + UART_LSR) & LSR_DATA_READY);
}

void serial_wait_for_read(u16 port)
{
   while (!serial_read_ready(port)) { }
}

char serial_read(u16 port)
{
   serial_wait_for_read(port);
   return inb(port);
}

bool serial_write_ready(u16 port)
{
   return !!(inb(port + UART_LSR) & LSR_EMPTY_TR_REG);
}

void serial_wait_for_write(u16 port)
{
   while (!serial_write_ready(port)) { }
}

void serial_write(u16 port, char c)
{
   serial_wait_for_write(port);
   outb(port, c);
}
