/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Interconnect framework internal structs
 *
 * Copyright (c) 2019, Linaro Ltd.
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#ifndef __DRIVERS_INTERCONNECT_INTERNAL_H
#define __DRIVERS_INTERCONNECT_INTERNAL_H

/**
 * struct icc_client_path - structure to hold client path information
 * @kobj: kobj used for uniquely storing/showing the limit parameters
 *        associated with path
 * @dev: client's dev node pointer
 * @limit_ab: ab bw value in KBpS used for limiting client voting
 * @limit_ib: ib bw value in KBpS used for limiting client voting
 * @commit: used to enforce the limit on client voting on path
 * @kobj_inited: bool to indicate if kobj init
 */
struct icc_client_path {
	struct icc_path *path;
	struct kobject kobj;
	u32 limit_ab;
	u32 limit_ib;
	bool commit;
	bool kobj_inited;
};

/**
 * struct icc_client - structure to hold client path and limit information
 * @client_list: list to hold the clients
 * @kobj: kobj used for displaying the client list in sysfs
 * @dev: client's dev node pointer
 * @num_paths : number of paths used by client
 * @kobj_inited: bool to indicate if kobj init
 * @paths: paths used by the client
 */
struct icc_client {
	struct list_head client_list;
	struct kobject kobj;
	struct device *dev;
	size_t num_paths;
	bool kobj_inited;
	struct icc_client_path paths[] __counted_by(num_paths);
};

/**
 * struct icc_req - constraints that are attached to each node
 * @req_node: entry in list of requests for the particular @node
 * @node: the interconnect node to which this constraint applies
 * @dev: reference to the device that sets the constraints
 * @enabled: indicates whether the path with this request is enabled
 * @tag: path tag (optional)
 * @avg_bw: an integer describing the average bandwidth in kBps
 * @peak_bw: an integer describing the peak bandwidth in kBps
 * @limit_ab: ab bw value in KBpS used for limiting client voting
 * @limit_ib: ib bw value in KBpS used for limiting client voting
 */
struct icc_req {
	struct hlist_node req_node;
	struct icc_node *node;
	struct device *dev;
	bool enabled;
	u32 tag;
	u32 avg_bw;
	u32 peak_bw;
	u32 limit_ab;
	u32 limit_ib;
};

/**
 * struct icc_path - interconnect path structure
 * @name: a string name of the path (useful for ftrace)
 * @num_nodes: number of hops (nodes)
 * @reqs: array of the requests applicable to this path of nodes
 */
struct icc_path {
	const char *name;
	size_t num_nodes;
	struct icc_req reqs[] __counted_by(num_nodes);
};

struct icc_path *icc_get(struct device *dev, const char *src, const char *dst);
int icc_debugfs_client_init(struct dentry *icc_dir);

#endif
