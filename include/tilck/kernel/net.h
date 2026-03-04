/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>

struct mac_addr {
    u8 data[6];
};

struct net_driver_funcs {
    struct mac_addr (*get_mac_addr)(void);
    int (*send_frame)(char *src, u32 len);
};

/*
 * Filled out by the driver
 */
extern struct net_driver_funcs net_driver_funcs;

/*
 * Called by the driver
 */
void net_process_packet(void *src, size_t len);