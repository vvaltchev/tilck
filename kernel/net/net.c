/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/net.h>
#include <tilck/common/printk.h>

struct net_driver_funcs net_driver_funcs;

void
net_process_packet(void *src, size_t len)
{
    (void) src;
    (void) len;
    /*
     * Packets not being processed yet
     */
}
