/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCIe bandwidth controller
 *
 * Copyright (C) 2023-2024 Intel Corporation
 */

#ifndef LINUX_PCI_BWCTRL_H
#define LINUX_PCI_BWCTRL_H

#include <linux/pci.h>

struct thermal_cooling_device;

#ifdef CONFIG_PCIE_THERMAL
struct thermal_cooling_device *pcie_cooling_device_register(struct pci_dev *port);
void pcie_cooling_device_unregister(struct thermal_cooling_device *cdev);
#else
static inline struct thermal_cooling_device *pcie_cooling_device_register(struct pci_dev *port)
{
	return NULL;
}
static inline void pcie_cooling_device_unregister(struct thermal_cooling_device *cdev)
{
}
#endif

/**
 * pcie_bwctrl_register - Opt an endpoint into bwctrl bandwidth scaling
 * @pdev:	Downstream PCIe device (e.g. an NVMe controller's own pci_dev)
 *
 * Call once from the endpoint driver's probe(). Every endpoint beneath a
 * given ancestor Switch/Root Port must call this before that ancestor's
 * Link Speed is ever scaled up or down; a single non-participating
 * endpoint (including one whose driver never calls this at all) holds
 * every ancestor up to the Root Port at its current speed. Safe to call
 * unconditionally; a no-op when CONFIG_PCIE_BW_ONDEMAND is disabled.
 */
#ifdef CONFIG_PCIE_BW_ONDEMAND
void pcie_bwctrl_register(struct pci_dev *pdev);
#else
static inline void pcie_bwctrl_register(struct pci_dev *pdev)
{
}
#endif

/**
 * pcie_bwctrl_note_activity - Report I/O byte transitions to bwctrl
 * @pdev:	Downstream PCIe device (e.g. an NVMe controller's own pci_dev)
 * @bytes:	Positive request size at submit, negative (matching) size at completion
 *
 * Callers call this once at submit with the request's size, and once at
 * completion with the negated size. bwctrl uses the resulting bytes-per-poll
 * throughput, together with the devfreq simple_ondemand governor, to scale
 * the upstream Root Port's Link Speed up or down. Safe to call
 * unconditionally; a no-op when CONFIG_PCIE_BW_ONDEMAND is disabled or the
 * port has no bwctrl instance.
 */
#ifdef CONFIG_PCIE_BW_ONDEMAND
void pcie_bwctrl_note_activity(struct pci_dev *pdev, s64 bytes);
#else
static inline void pcie_bwctrl_note_activity(struct pci_dev *pdev, s64 bytes)
{
}
#endif

#endif
