/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * e1000 Driver Configurations
 */
#pragma once

/*
 * Number of slots in the transmission and
 * receive queues
 */
#define TX_RING_CAP     32
#define RX_RING_CAP     32

/*
 * Size in bytes of each slot of the transmission and
 * receive queues.
 *
 * These sizes must be a port of two between 256 and
 * 16384 (inclusive). We go with 2K as ethernet frames
 * are around 1.5K bytes.
 */
#define TX_BUF_SIZE   2048
#define RX_BUF_SIZE   2048
