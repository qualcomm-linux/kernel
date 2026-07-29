.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

===============================================================================
QCOM System Control and Management Interface(SCMI) Vendor Protocols Extension
===============================================================================

:Copyright: |copy| Qualcomm Technologies, Inc. and/or its subsidiaries.

:Author:
   - Sibi Sankar <sibi.sankar@oss.qualcomm.com>
   - Pragnesh Papaniya <pragnesh.papaniya@oss.qualcomm.com>

SCMI_GENERIC: System Control and Management Interface QCOM Generic Vendor Protocol
==================================================================================

This protocol is intended as a generic way of exposing a number of Qualcomm
SoC specific features through a mixture of pre-determined algorithm string and
param_id pairs hosted on the SCMI controller. It implements an interface compliant
with the Arm SCMI Specification with additional vendor specific commands as
detailed below.

Supported algorithm strings are documented in their own sections after the
generic commands (currently: MEMLAT, see below).

Commands:
_________

PROTOCOL_VERSION
~~~~~~~~~~~~~~~~

message_id: 0x0
protocol_id: 0x80

+---------------+--------------------------------------------------------------+
|Return values                                                                 |
+---------------+--------------------------------------------------------------+
|Name           |Description                                                   |
+---------------+--------------------------------------------------------------+
|int32 status   |See ARM SCMI Specification for status code definitions.       |
+---------------+--------------------------------------------------------------+
|uint32 version |For this revision of the specification, this value must be    |
|               |0x10000.                                                      |
+---------------+--------------------------------------------------------------+

PROTOCOL_ATTRIBUTES
~~~~~~~~~~~~~~~~~~~

message_id: 0x1
protocol_id: 0x80

+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |See ARM SCMI Specification for status code definitions.    |
+------------------+-----------------------------------------------------------+
|uint32 attributes |Bits[31:16] Reserved, must be set to 0.                    |
|                  |Bits[15:8] Number of agents in the system. Must match the  |
|                  |value reported by the standard BASE protocol's             |
|                  |PROTOCOL_ATTRIBUTES response.                              |
|                  |Bits[7:0] Number of algorithmic strings supported by the   |
|                  |system. Only "MEMLAT" is currently supported hence it      |
|                  |returns 1.                                                 |
+------------------+-----------------------------------------------------------+

PROTOCOL_MESSAGE_ATTRIBUTES
~~~~~~~~~~~~~~~~~~~~~~~~~~~

message_id: 0x2
protocol_id: 0x80

+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |See ARM SCMI Specification for status code definitions.    |
+------------------+-----------------------------------------------------------+
|uint32 attributes |For all message IDs the parameter has a value of 0.        |
+------------------+-----------------------------------------------------------+

QCOM_SCMI_SET_PARAM
~~~~~~~~~~~~~~~~~~~

message_id: 0x10
protocol_id: 0x80

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 ext_id     |Reserved, must be zero.                                    |
+------------------+-----------------------------------------------------------+
|uint32 algo_low   |Lower 32-bit value of the algorithm string.                |
+------------------+-----------------------------------------------------------+
|uint32 algo_high  |Upper 32-bit value of the algorithm string.                |
+------------------+-----------------------------------------------------------+
|uint32 param_id   |Serves as the token message id for the algorithm string    |
|                  |and is used to set various parameters supported by it.     |
+------------------+-----------------------------------------------------------+
|uint32 buf[]      |Serves as the payload for the specified param_id and       |
|                  |algorithm string pair. The payload size depends on the     |
|                  |(algorithm string, param_id) pair; see the per-algorithm   |
|                  |sections below.                                            |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if the param_id and buf[] is parsed successfully  |
|                  |by the chosen algorithm string.                            |
|                  |NOT_SUPPORTED: if the algorithm string does not have any   |
|                  |matches.                                                   |
|                  |INVALID_PARAMETERS: if the param_id and the buf[] passed   |
|                  |is rejected by the algorithm string.                       |
+------------------+-----------------------------------------------------------+

QCOM_SCMI_GET_PARAM
~~~~~~~~~~~~~~~~~~~

message_id: 0x11
protocol_id: 0x80

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 ext_id     |Reserved, must be zero.                                    |
+------------------+-----------------------------------------------------------+
|uint32 algo_low   |Lower 32-bit value of the algorithm string.                |
+------------------+-----------------------------------------------------------+
|uint32 algo_high  |Upper 32-bit value of the algorithm string.                |
+------------------+-----------------------------------------------------------+
|uint32 param_id   |Serves as the token message id for the algorithm string.   |
+------------------+-----------------------------------------------------------+
|uint32 buf[]      |Serves as the payload and store of value for the specified |
|                  |param_id and algorithm string pair. The payload size       |
|                  |depends on the (algorithm string, param_id) pair; see the  |
|                  |per-algorithm sections below. The response payload is      |
|                  |returned in the same buffer, overwriting the request       |
|                  |contents on success.                                       |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if the param_id and buf[] is parsed successfully  |
|                  |by the chosen algorithm string and the result is copied    |
|                  |into buf[].                                                |
|                  |NOT_SUPPORTED: if the algorithm string does not have any   |
|                  |matches.                                                   |
|                  |INVALID_PARAMETERS: if the param_id and the buf[] passed   |
|                  |is rejected by the algorithm string.                       |
+------------------+-----------------------------------------------------------+
|uint32 buf[]      |Holds the payload of the result of the query, returned in  |
|                  |the same buffer used to send the request. Size depends on  |
|                  |the (algorithm string, param_id) pair.                     |
+------------------+-----------------------------------------------------------+

QCOM_SCMI_START_ACTIVITY
~~~~~~~~~~~~~~~~~~~~~~~~

message_id: 0x12
protocol_id: 0x80

The activity to be started is defined by the algorithm string; see the
per-algorithm sections (e.g. MEMLAT_START_TIMER) for valid param_ids.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 ext_id     |Reserved, must be zero.                                    |
+------------------+-----------------------------------------------------------+
|uint32 algo_low   |Lower 32-bit value of the algorithm string.                |
+------------------+-----------------------------------------------------------+
|uint32 algo_high  |Upper 32-bit value of the algorithm string.                |
+------------------+-----------------------------------------------------------+
|uint32 param_id   |Serves as the token message id for the algorithm string    |
|                  |and is generally used to start the activity performed by   |
|                  |the algorithm string.                                      |
+------------------+-----------------------------------------------------------+
|uint32 buf[]      |Serves as the payload for the specified param_id and       |
|                  |algorithm string pair. The payload size depends on the     |
|                  |(algorithm string, param_id) pair; see the per-algorithm   |
|                  |sections below.                                            |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if the activity performed by the algorithm string |
|                  |starts successfully, or if it was already running.         |
|                  |NOT_SUPPORTED: if the algorithm string does not have any   |
|                  |matches.                                                   |
+------------------+-----------------------------------------------------------+

QCOM_SCMI_STOP_ACTIVITY
~~~~~~~~~~~~~~~~~~~~~~~

message_id: 0x13
protocol_id: 0x80

The activity to be stopped is defined by the algorithm string; see the
per-algorithm sections (e.g. MEMLAT_STOP_TIMER) for valid param_ids.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 ext_id     |Reserved, must be zero.                                    |
+------------------+-----------------------------------------------------------+
|uint32 algo_low   |Lower 32-bit value of the algorithm string.                |
+------------------+-----------------------------------------------------------+
|uint32 algo_high  |Upper 32-bit value of the algorithm string.                |
+------------------+-----------------------------------------------------------+
|uint32 param_id   |Serves as the token message id for the algorithm string    |
|                  |and is generally used to stop the activity performed by    |
|                  |the algorithm string.                                      |
+------------------+-----------------------------------------------------------+
|uint32 buf[]      |Serves as the payload for the specified param_id and       |
|                  |algorithm string pair. The payload size depends on the     |
|                  |(algorithm string, param_id) pair; see the per-algorithm   |
|                  |sections below.                                            |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if the activity performed by the algorithm string |
|                  |stops successfully, or if it was not running.              |
|                  |NOT_SUPPORTED: if the algorithm string does not have any   |
|                  |matches.                                                   |
+------------------+-----------------------------------------------------------+

MEMLAT: Memory Latency algorithm
================================

The MEMLAT algorithm string (0x4D454D4C4154, ASCII "MEMLAT") is hosted on
the Qualcomm Generic Vendor Protocol and runs on the CPUCP firmware. The
6-byte value is treated as a 64-bit algorithm string and split into two
uint32 fields on the wire: algo_low carries its lower 32 bits and algo_high
its upper 32 bits.
It samples per-CPU performance counters at a configurable period, computes
per memory-group statistics (Instructions-Per-Miss, back-end stall, etc.),
and votes the resulting target frequency to the bus interconnect for DDR,
LLCC and DDR_QOS. Userspace control of the algorithm is exposed via
parameter IDs issued through QCOM_SCMI_SET_PARAM and QCOM_SCMI_GET_PARAM.

The hw_type field carried in most payloads identifies the memory group:

+----------+--------------------------------------------------------------+
|hw_type   |Group                                                         |
+----------+--------------------------------------------------------------+
|0         |DDR                                                           |
+----------+--------------------------------------------------------------+
|1         |LLCC                                                          |
+----------+--------------------------------------------------------------+
|2         |DDR_QOS                                                       |
+----------+--------------------------------------------------------------+

All multi-byte fields below are little-endian, in line with the SCMI
specification. mon_idx selects a monitor within the group (0-based, less
than the firmware-supported maximum). All SET commands return the SCMI
status word; on success it carries SUCCESS, on lookup failure
INVALID_PARAMETERS, and on an unknown param_id NOT_SUPPORTED.

Frequency units differ per command: MEMLAT_SET_MIN_FREQ and
MEMLAT_SET_MAX_FREQ take kHz for DDR/LLCC, whereas MEMLAT_MON_FREQ_MAP
expresses the resulting vote (v2) in MHz for DDR/LLCC. For DDR_QOS all of
these carry a raw level index (0 / 1) rather than a frequency.

MEMLAT_SET_MEM_GROUP
~~~~~~~~~~~~~~~~~~~~

param_id: 0x10 (16)
command:  QCOM_SCMI_SET_PARAM

Allocates a new memory group on the firmware side and binds it to the
underlying interconnect path (DDR / LLCC / DDR_QOS).

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 cpumask    |Bitmask of HW CPU IDs that contribute counters to this     |
|                  |group (limited to 32 CPUs).                                |
+------------------+-----------------------------------------------------------+
|uint32 hw_type    |Memory group identifier (0 = DDR, 1 = LLCC, 2 = DDR_QOS).  |
+------------------+-----------------------------------------------------------+
|uint32 mon_type   |Reserved for SET_MEM_GROUP (set to 0; populated only on    |
|                  |SET_MONITOR).                                              |
+------------------+-----------------------------------------------------------+
|uint32 mon_idx    |Reserved for SET_MEM_GROUP (set to 0).                     |
+------------------+-----------------------------------------------------------+
|char mon_name[20] |Reserved for SET_MEM_GROUP (zero-filled).                  |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: group allocated.                                  |
|                  |BUSY: no free memory group slot (the firmware-supported    |
|                  |maximum number of groups is already configured).           |
+------------------+-----------------------------------------------------------+

MEMLAT_SET_MONITOR
~~~~~~~~~~~~~~~~~~

param_id: 0x11 (17)
command:  QCOM_SCMI_SET_PARAM

Adds a monitor (a CPU subset that votes within an already-configured
group).

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 cpumask    |Bitmask of HW CPU IDs assigned to this monitor (must be a  |
|                  |subset of the group's cpumask; limited to 32 CPUs).        |
+------------------+-----------------------------------------------------------+
|uint32 hw_type    |Memory group identifier the monitor belongs to.            |
+------------------+-----------------------------------------------------------+
|uint32 mon_type   |0 = IPM-based latency monitor, 1 = compute (stall-only)    |
|                  |monitor.                                                   |
+------------------+-----------------------------------------------------------+
|uint32 mon_idx    |Index of the monitor within the group.                     |
+------------------+-----------------------------------------------------------+
|char mon_name[20] |Human-readable monitor name (NUL-terminated, used in       |
|                  |firmware log lines).                                       |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: monitor created.                                  |
|                  |NOT_FOUND: hw_type does not match any configured group, or |
|                  |the firmware-supported maximum number of monitors already  |
|                  |exist for the group.                                       |
+------------------+-----------------------------------------------------------+

MEMLAT_SET_COMMON_EV_MAP
~~~~~~~~~~~~~~~~~~~~~~~~

param_id: 0x12 (18)
command:  QCOM_SCMI_SET_PARAM

Configures the common counter IDs (instructions, cycles, stall, etc.)
that the firmware reads on every sample for every CPU.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 num_evs    |Number of valid entries in cid[].                          |
+------------------+-----------------------------------------------------------+
|uint32 hw_type    |Set to 0xFF (sentinel for the common-events case).         |
+------------------+-----------------------------------------------------------+
|uint8 cid[]       |Array of CPUCP counter IDs indexed by INST/CYC/CONST_CYC/  |
|                  |FE_STALL/BE_STALL. 0xFF marks an unused slot.              |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS / INVALID_PARAMETERS if num_evs exceeds the        |
|                  |firmware-supported maximum.                                |
+------------------+-----------------------------------------------------------+

MEMLAT_SET_GRP_EV_MAP
~~~~~~~~~~~~~~~~~~~~~

param_id: 0x13 (19)
command:  QCOM_SCMI_SET_PARAM

Configures the per-group event IDs (cache miss / writeback / access)
used by the IPM and write-back computations for the selected hw_type.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 num_evs    |Number of valid entries in cid[].                          |
+------------------+-----------------------------------------------------------+
|uint32 hw_type    |Memory group identifier.                                   |
+------------------+-----------------------------------------------------------+
|uint8 cid[]       |Array of CPUCP counter IDs indexed by MISS/WB/ACC. 0xFF    |
|                  |marks an unused slot.                                      |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS / INVALID_PARAMETERS if hw_type is unknown or      |
|                  |num_evs exceeds the firmware-supported maximum.            |
+------------------+-----------------------------------------------------------+

MEMLAT_IPM_CEIL
~~~~~~~~~~~~~~~

param_id: 0x17 (23)
command:  QCOM_SCMI_SET_PARAM

Sets the IPM (Instructions-Per-Miss) ceiling for a monitor. CPUs whose
IPM falls at or below this ceiling are considered memory-bound and
contribute their cpufreq into the monitor's voting pool.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 hw_type    |Memory group identifier.                                   |
+------------------+-----------------------------------------------------------+
|uint32 mon_idx    |Monitor index within the group.                            |
+------------------+-----------------------------------------------------------+
|uint32 val        |IPM ceiling (instructions per cache miss).                 |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS / INVALID_PARAMETERS if (hw_type, mon_idx) does    |
|                  |not match a registered monitor.                            |
+------------------+-----------------------------------------------------------+

MEMLAT_BE_STALL_FLOOR
~~~~~~~~~~~~~~~~~~~~~

param_id: 0x19 (25)
command:  QCOM_SCMI_SET_PARAM

Sets the back-end stall floor (in milli-percent: 100000 = 100%) for a
monitor. CPUs whose back-end stall is at or above this floor are
eligible to contribute their cpufreq even if their IPM is above the
ceiling.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 hw_type    |Memory group identifier.                                   |
+------------------+-----------------------------------------------------------+
|uint32 mon_idx    |Monitor index within the group.                            |
+------------------+-----------------------------------------------------------+
|uint32 val        |Back-end stall floor in milli-percent (0..100000).         |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS / INVALID_PARAMETERS if (hw_type, mon_idx) does    |
|                  |not match a registered monitor.                            |
+------------------+-----------------------------------------------------------+

MEMLAT_SAMPLE_MS
~~~~~~~~~~~~~~~~

param_id: 0x1F (31)
command:  QCOM_SCMI_SET_PARAM

Sets the sampling period (in milliseconds) used by the firmware update
loop.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 sample_ms   |Sampling period in milliseconds.                           |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS.                                                   |
+------------------+-----------------------------------------------------------+

MEMLAT_MON_FREQ_MAP
~~~~~~~~~~~~~~~~~~~

param_id: 0x20 (32)
command:  QCOM_SCMI_SET_PARAM

Programs the cpufreq to memfreq voting table for a single monitor.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 hw_type    |Memory group identifier.                                   |
+------------------+-----------------------------------------------------------+
|uint32 mon_idx    |Monitor index within the group.                            |
+------------------+-----------------------------------------------------------+
|uint32 nr_rows    |Number of valid rows in tbl[] (must not exceed the         |
|                  |firmware-supported maximum).                               |
+------------------+-----------------------------------------------------------+
|struct {          |Per-row mapping. v1 is the cpufreq trigger in MHz; v2 is   |
|  uint16 v1;      |the resulting memfreq vote (MHz for DDR/LLCC, a level      |
|  uint16 v2;      |index 0/1 for DDR_QOS).                                    |
|} tbl[]           |                                                           |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS / INVALID_PARAMETERS if (hw_type, mon_idx) is      |
|                  |unknown / OUT_OF_RANGE if nr_rows exceeds the              |
|                  |firmware-supported maximum.                                |
+------------------+-----------------------------------------------------------+

MEMLAT_SET_MIN_FREQ
~~~~~~~~~~~~~~~~~~~

param_id: 0x21 (33)
command:  QCOM_SCMI_SET_PARAM

Clamps a monitor's vote to a minimum value. Units are kHz for DDR/LLCC and
raw level index for DDR_QOS.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 hw_type    |Memory group identifier.                                   |
+------------------+-----------------------------------------------------------+
|uint32 mon_idx    |Monitor index within the group.                            |
+------------------+-----------------------------------------------------------+
|uint32 val        |Minimum frequency: kHz for DDR/LLCC, level for DDR_QOS.    |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS / INVALID_PARAMETERS if (hw_type, mon_idx) does    |
|                  |not match a registered monitor.                            |
+------------------+-----------------------------------------------------------+

MEMLAT_SET_MAX_FREQ
~~~~~~~~~~~~~~~~~~~

param_id: 0x22 (34)
command:  QCOM_SCMI_SET_PARAM

Clamps a monitor's vote to a maximum value. Units identical to
MEMLAT_SET_MIN_FREQ.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 hw_type    |Memory group identifier.                                   |
+------------------+-----------------------------------------------------------+
|uint32 mon_idx    |Monitor index within the group.                            |
+------------------+-----------------------------------------------------------+
|uint32 val        |Maximum frequency: kHz for DDR/LLCC, level for DDR_QOS.    |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS / INVALID_PARAMETERS if (hw_type, mon_idx) does    |
|                  |not match a registered monitor.                            |
+------------------+-----------------------------------------------------------+

MEMLAT_GET_CUR_FREQ
~~~~~~~~~~~~~~~~~~~

param_id: 0x23 (35)
command:  QCOM_SCMI_GET_PARAM

Reads the current target frequency that the firmware is voting for the
selected (hw_type, mon_idx) tuple. The response payload is returned in
the same buffer used to send the request, overwriting it on success.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 hw_type    |Memory group identifier.                                   |
+------------------+-----------------------------------------------------------+
|uint32 mon_idx    |Monitor index within the group.                            |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS / INVALID_PARAMETERS if (hw_type, mon_idx) does    |
|                  |not match a registered monitor.                            |
+------------------+-----------------------------------------------------------+
|uint32 cur_freq   |Current target frequency in kHz for DDR/LLCC; raw level    |
|                  |(0/1) for DDR_QOS.                                         |
+------------------+-----------------------------------------------------------+

MEMLAT_START_TIMER
~~~~~~~~~~~~~~~~~~

param_id: 0x24 (36)
command:  QCOM_SCMI_START_ACTIVITY

Starts the firmware sampling and voting loop at the configured
sample_ms interval. Has no payload beyond the QCOM_SCMI_START_ACTIVITY
header.

+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: timer started (or was already running).           |
|                  |GENERIC_ERROR: events not yet initialized                  |
|                  |(MEMLAT_SET_GRP_EV_MAP not called for any group).          |
|                  |NOT_SUPPORTED: invalid param_id under START_ACTIVITY.      |
+------------------+-----------------------------------------------------------+

MEMLAT_STOP_TIMER
~~~~~~~~~~~~~~~~~

param_id: 0x25 (37)
command:  QCOM_SCMI_STOP_ACTIVITY

Suspends the firmware sampling and voting loop. Has no payload beyond
the QCOM_SCMI_STOP_ACTIVITY header. The configured monitors and freq
maps are retained, so a subsequent MEMLAT_START_TIMER resumes voting
without re-programming.

+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: timer stopped (or was already stopped).           |
|                  |NOT_SUPPORTED: invalid param_id under STOP_ACTIVITY.       |
+------------------+-----------------------------------------------------------+

MEMLAT_SET_EFFECTIVE_FREQ_METHOD
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

param_id: 0x27 (39)
command:  QCOM_SCMI_SET_PARAM

Selects the algorithm used to derive the per-CPU effective frequency
from the cycle counters.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 method      |0: const-cycles method (CPU cycles / const-cycles, scaled  |
|                  |by the cluster's max frequency).                           |
|                  |1: legacy method (CPU cycles / sampling-window time).      |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS / INVALID_PARAMETERS if method is not 0 or 1.      |
+------------------+-----------------------------------------------------------+
