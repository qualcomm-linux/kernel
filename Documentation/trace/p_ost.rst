.. SPDX-License-Identifier: GPL-2.0

===================
MIPI OST over STP
===================

The OST (Open System Trace) driver is used with STM class devices to
generate standardized trace stream. Trace sources can be identified
by different entity IDs.

CONFIG_STM_PROTO_OST is for p_ost driver enablement. Once this config
is enabled, you can select the p_ost protocol by command below:

# mkdir /sys/kernel/config/stp-policy/stm0:p_ost.policy

The policy name format is extended like this:

  <device_name>:<protocol_name>.<policy_name>

With a coresight-stm device, it will look like "stm0:p_ost.policy".

With the MIPI OST protocol driver, the attributes for each protocol node are:

# mkdir /sys/kernel/config/stp-policy/stm0:p_ost.policy/default
# ls /sys/kernel/config/stp-policy/stm0:p_ost.policy/default
channels  entity    masters

The entity here is the set of entities that p_ost supports. Currently
p_ost supports ftrace, console and diag entities.

Set entity:
# echo 'ftrace' > /sys/kernel/config/stp-policy/stm0:p_ost.policy/default/entity

Get available and currently selected (shown in square brackets) entity:
# cat /sys/kernel/config/stp-policy/stm0:p_ost.policy/default/entity
[ftrace] console diag

See Documentation/ABI/testing/configfs-stp-policy-p_ost for more details.

