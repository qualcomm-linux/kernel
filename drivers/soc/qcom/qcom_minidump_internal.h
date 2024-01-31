/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_MINIDUMP_INTERNAL_H_
#define _QCOM_MINIDUMP_INTERNAL_H_

#define MAX_NUM_OF_SS           10
#define MAX_REGION_NAME_LENGTH  16
#define SBL_MINIDUMP_SMEM_ID	602

#define MINIDUMP_REGION_VALID	   ('V' << 24 | 'A' << 16 | 'L' << 8 | 'I' << 0)
#define MINIDUMP_REGION_INVALID	   ('I' << 24 | 'N' << 16 | 'V' << 8 | 'A' << 0)
#define MINIDUMP_REGION_INIT	   ('I' << 24 | 'N' << 16 | 'I' << 8 | 'T' << 0)
#define MINIDUMP_REGION_NOINIT	   0

#define MINIDUMP_SS_ENCR_REQ	   (0 << 24 | 'Y' << 16 | 'E' << 8 | 'S' << 0)
#define MINIDUMP_SS_ENCR_NOTREQ	   (0 << 24 | 0 << 16 | 'N' << 8 | 'R' << 0)
#define MINIDUMP_SS_ENCR_START	   ('S' << 24 | 'T' << 16 | 'R' << 8 | 'T' << 0)
#define MINIDUMP_SS_ENCR_DONE	   ('D' << 24 | 'O' << 16 | 'N' << 8 | 'E' << 0)
#define MINIDUMP_SS_ENABLED	   ('E' << 24 | 'N' << 16 | 'B' << 8 | 'L' << 0)

#define MINIDUMP_APSS_DESC	   0

/**
 * struct minidump_region - Minidump region
 * @name		: Name of the region to be dumped
 * @seq_num:		: Use to differentiate regions with same name.
 * @valid		: This entry to be dumped (if set to 1)
 * @address		: Physical address of region to be dumped
 * @size		: Size of the region
 */
struct minidump_region {
	char	name[MAX_REGION_NAME_LENGTH];
	__le32	seq_num;
	__le32	valid;
	__le64	address;
	__le64	size;
};

/**
 * struct minidump_subsystem - Subsystem's SMEM Table of content
 * @status : Subsystem toc init status
 * @enabled : if set to 1, this region would be copied during coredump
 * @encryption_status: Encryption status for this subsystem
 * @encryption_required : Decides to encrypt the subsystem regions or not
 * @region_count : Number of regions added in this subsystem toc
 * @regions_baseptr : regions base pointer of the subsystem
 */
struct minidump_subsystem {
	__le32	status;
	__le32	enabled;
	__le32	encryption_status;
	__le32	encryption_required;
	__le32	region_count;
	__le64	regions_baseptr;
};

/**
 * struct minidump_global_toc - Global Table of Content
 * @status : Global Minidump init status
 * @md_revision : Minidump revision
 * @enabled : Minidump enable status
 * @subsystems : Array of subsystems toc
 */
struct minidump_global_toc {
	__le32				status;
	__le32				md_revision;
	__le32				enabled;
	struct minidump_subsystem	subsystems[MAX_NUM_OF_SS];
};

#endif /* _QCOM_MINIDUMP_INTERNAL_H_ */
