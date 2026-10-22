/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _NET_QRTR_H
#define _NET_QRTR_H

#include <linux/types.h>

/* Node ids at and above this base are reserved for the host and are never
 * accepted from an endpoint. The base is well above the ids used by the
 * remote nodes, so that pre-existing node ids stay untouched.
 */
#define QRTR_NODE_HOST_BASE 0x10000u

/* Compute host node id from a per-device index. The index must be unique
 * among the host assigned endpoints and smaller than QRTR_NODE_HOST_BASE.
 */
static inline unsigned int qrtr_host_node_id(unsigned int index)
{
	return QRTR_NODE_HOST_BASE + index;
}

#endif /* _NET_QRTR_H */
