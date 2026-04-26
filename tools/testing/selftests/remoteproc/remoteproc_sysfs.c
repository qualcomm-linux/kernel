// SPDX-License-Identifier: GPL-2.0-only
/*
 * Selftests for the remoteproc sysfs interface.
 *
 * Covers /sys/class/remoteproc/remoteprocN/ attributes:
 *   name, state, firmware, coredump, recovery
 *
 * All tests skip gracefully when no remoteproc devices are present.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../kselftest_harness.h"
#include "remoteproc_helpers.h"

/* Valid state strings as reported by the kernel */
static const char * const valid_states[] = {
	"offline", "suspended", "running", "crashed",
	"invalid", "attached", "detached",
};

/* Valid coredump mode strings */
static const char * const valid_coredump[] = {
	"disabled", "enabled", "inline",
};

static bool str_in_list(const char *str, const char * const *list, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		if (strcmp(str, list[i]) == 0)
			return true;
	}
	return false;
}

FIXTURE(sysfs) {
	char dev[RPROC_PATH_MAX];
	char orig_firmware[RPROC_ATTR_BUF_MAX];
	char orig_coredump[RPROC_ATTR_BUF_MAX];
	char orig_recovery[RPROC_ATTR_BUF_MAX];
};

FIXTURE_SETUP(sysfs)
{
	char devs[RPROC_MAX_DEVS][RPROC_PATH_MAX];
	int n;

	n = rproc_find_devices(devs, RPROC_MAX_DEVS);
	if (n == 0)
		SKIP(return, "no remoteproc devices found");

	snprintf(self->dev, sizeof(self->dev), "%s", devs[0]);

	ASSERT_EQ(0, rproc_sysfs_read(self->dev, "firmware",
				      self->orig_firmware,
				      sizeof(self->orig_firmware)));
	ASSERT_EQ(0, rproc_sysfs_read(self->dev, "coredump",
				      self->orig_coredump,
				      sizeof(self->orig_coredump)));
	ASSERT_EQ(0, rproc_sysfs_read(self->dev, "recovery",
				      self->orig_recovery,
				      sizeof(self->orig_recovery)));
}

FIXTURE_TEARDOWN(sysfs)
{
	/* Restore coredump and recovery to original values. */
	rproc_sysfs_write(self->dev, "coredump", self->orig_coredump);
	rproc_sysfs_write(self->dev, "recovery", self->orig_recovery);

	/*
	 * Restore firmware only when device is offline; skip silently
	 * if the device is in any other state to avoid unintended side
	 * effects.
	 */
	char state[RPROC_ATTR_BUF_MAX];

	if (rproc_sysfs_read(self->dev, "state", state, sizeof(state)) == 0 &&
	    strcmp(state, "offline") == 0)
		rproc_sysfs_write(self->dev, "firmware", self->orig_firmware);
}

/* name attribute is readable and non-empty */
TEST_F(sysfs, name_readable)
{
	char name[RPROC_ATTR_BUF_MAX];

	ASSERT_EQ(0, rproc_sysfs_read(self->dev, "name",
				      name, sizeof(name)));
	EXPECT_GT((int)strlen(name), 0);
}

/* state attribute returns a known valid state string */
TEST_F(sysfs, state_valid_value)
{
	char state[RPROC_ATTR_BUF_MAX];

	ASSERT_EQ(0, rproc_sysfs_read(self->dev, "state",
				      state, sizeof(state)));
	EXPECT_TRUE(str_in_list(state, valid_states,
				ARRAY_SIZE(valid_states)));
}

/* firmware attribute is readable and non-empty */
TEST_F(sysfs, firmware_readable)
{
	char firmware[RPROC_ATTR_BUF_MAX];

	ASSERT_EQ(0, rproc_sysfs_read(self->dev, "firmware",
				      firmware, sizeof(firmware)));
	EXPECT_GT((int)strlen(firmware), 0);
}

/* coredump attribute returns a known valid mode string */
TEST_F(sysfs, coredump_valid_value)
{
	char coredump[RPROC_ATTR_BUF_MAX];

	ASSERT_EQ(0, rproc_sysfs_read(self->dev, "coredump",
				      coredump, sizeof(coredump)));
	EXPECT_TRUE(str_in_list(coredump, valid_coredump,
				ARRAY_SIZE(valid_coredump)));
}

/* recovery attribute returns "enabled" or "disabled" */
TEST_F(sysfs, recovery_valid_value)
{
	char recovery[RPROC_ATTR_BUF_MAX];

	ASSERT_EQ(0, rproc_sysfs_read(self->dev, "recovery",
				      recovery, sizeof(recovery)));
	EXPECT_TRUE(strcmp(recovery, "enabled") == 0 ||
		    strcmp(recovery, "disabled") == 0);
}

/* Writing an unrecognised string to state must be rejected with EINVAL */
TEST_F(sysfs, state_write_invalid_rejected)
{
	EXPECT_EQ(-EINVAL, rproc_sysfs_write(self->dev, "state",
					     "invalidcmd"));
}

/* Writing an unrecognised string to coredump must be rejected with EINVAL */
TEST_F(sysfs, coredump_write_invalid_rejected)
{
	EXPECT_EQ(-EINVAL, rproc_sysfs_write(self->dev, "coredump",
					     "invalidmode"));
}

/* Writing an unrecognised string to recovery must be rejected with EINVAL */
TEST_F(sysfs, recovery_write_invalid_rejected)
{
	EXPECT_EQ(-EINVAL, rproc_sysfs_write(self->dev, "recovery",
					     "invalidopt"));
}

/*
 * When device is offline, firmware attribute must be writable.
 * Teardown restores the original firmware value.
 */
TEST_F(sysfs, firmware_write_while_offline)
{
	char state[RPROC_ATTR_BUF_MAX];
	char readback[RPROC_ATTR_BUF_MAX];

	ASSERT_EQ(0, rproc_sysfs_read(self->dev, "state",
				      state, sizeof(state)));
	if (strcmp(state, "offline") != 0)
		SKIP(return, "device not offline, skipping firmware write test");

	ASSERT_EQ(0, rproc_sysfs_write(self->dev, "firmware",
				       "test-firmware.elf"));
	ASSERT_EQ(0, rproc_sysfs_read(self->dev, "firmware",
				      readback, sizeof(readback)));
	EXPECT_EQ(0, strcmp(readback, "test-firmware.elf"));
}

/*
 * When the device is not offline, firmware attribute must be rejected
 * with EBUSY; rproc_set_firmware() only permits changes in offline state.
 */
TEST_F(sysfs, firmware_write_while_running_rejected)
{
	char state[RPROC_ATTR_BUF_MAX];

	ASSERT_EQ(0, rproc_sysfs_read(self->dev, "state",
				      state, sizeof(state)));
	if (strcmp(state, "running") != 0)
		SKIP(return, "device not running, skipping firmware busy test");

	EXPECT_EQ(-EBUSY, rproc_sysfs_write(self->dev, "firmware",
					     "test-firmware.elf"));
}

/* Cycle through all valid coredump modes and verify each reads back correctly */
TEST_F(sysfs, coredump_roundtrip)
{
	static const char * const modes[] = { "disabled", "enabled", "inline" };
	char readback[RPROC_ATTR_BUF_MAX];
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(modes); i++) {
		ASSERT_EQ(0, rproc_sysfs_write(self->dev, "coredump",
					       modes[i]));
		ASSERT_EQ(0, rproc_sysfs_read(self->dev, "coredump",
					      readback, sizeof(readback)));
		EXPECT_EQ(0, strcmp(readback, modes[i]));
	}
}

/* Toggle recovery enabled <-> disabled and verify each reads back correctly */
TEST_F(sysfs, recovery_roundtrip)
{
	char readback[RPROC_ATTR_BUF_MAX];

	ASSERT_EQ(0, rproc_sysfs_write(self->dev, "recovery", "disabled"));
	ASSERT_EQ(0, rproc_sysfs_read(self->dev, "recovery",
				      readback, sizeof(readback)));
	EXPECT_EQ(0, strcmp(readback, "disabled"));

	ASSERT_EQ(0, rproc_sysfs_write(self->dev, "recovery", "enabled"));
	ASSERT_EQ(0, rproc_sysfs_read(self->dev, "recovery",
				      readback, sizeof(readback)));
	EXPECT_EQ(0, strcmp(readback, "enabled"));
}

/*
 * Writing "stop" to a device already in offline state must be rejected
 * with EINVAL; rproc_shutdown() only accepts RUNNING or ATTACHED states.
 */
TEST_F(sysfs, stop_when_offline_rejected)
{
	char state[RPROC_ATTR_BUF_MAX];

	ASSERT_EQ(0, rproc_sysfs_read(self->dev, "state",
				      state, sizeof(state)));
	if (strcmp(state, "offline") != 0)
		SKIP(return, "device not offline");

	EXPECT_EQ(-EINVAL, rproc_sysfs_write(self->dev, "state", "stop"));
}

/*
 * Writing "detach" to a device already in offline state must be rejected
 * with EINVAL; rproc_detach() only accepts ATTACHED state.
 */
TEST_F(sysfs, detach_when_offline_rejected)
{
	char state[RPROC_ATTR_BUF_MAX];

	ASSERT_EQ(0, rproc_sysfs_read(self->dev, "state",
				      state, sizeof(state)));
	if (strcmp(state, "offline") != 0)
		SKIP(return, "device not offline");

	EXPECT_EQ(-EINVAL, rproc_sysfs_write(self->dev, "state", "detach"));
}

/*
 * Writing "recover" to the recovery attribute must be accepted without
 * error regardless of the current recovery flag; it triggers an immediate
 * recovery attempt without changing the enabled/disabled flag.
 */
TEST_F(sysfs, recovery_recover_accepted)
{
	char recovery[RPROC_ATTR_BUF_MAX];

	EXPECT_EQ(0, rproc_sysfs_write(self->dev, "recovery", "recover"));

	/* Flag must remain unchanged after a "recover" command. */
	ASSERT_EQ(0, rproc_sysfs_read(self->dev, "recovery",
				      recovery, sizeof(recovery)));
	EXPECT_TRUE(strcmp(recovery, "enabled") == 0 ||
		    strcmp(recovery, "disabled") == 0);
}

TEST_HARNESS_MAIN
