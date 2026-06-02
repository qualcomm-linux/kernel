/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ACPI_AEST_H__
#define __ACPI_AEST_H__

#include <linux/acpi.h>

/* AEST resource name */
#define AEST_NODE_NAME "AEST:NODE"

/* AEST interface */
#define AEST_XFACE_FLAG_SHARED		BIT(0)
#define AEST_XFACE_FLAG_CLEAR_MISC	BIT(1)
#define AEST_XFACE_FLAG_ERROR_DEVICE	BIT(2)
#define AEST_XFACE_FLAG_AFFINITY	BIT(3)
#define AEST_XFACE_FLAG_ERROR_GROUP	BIT(4)
#define AEST_XFACE_FLAG_FAULT_INJECT	BIT(5)
#define AEST_XFACE_FLAG_INT_CONFIG	BIT(6)

#endif /* __ACPI_AEST_H__ */
