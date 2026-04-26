/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Helpers for remoteproc selftests: device enumeration and sysfs I/O.
 */
#ifndef REMOTEPROC_HELPERS_H
#define REMOTEPROC_HELPERS_H

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define RPROC_SYSFS_CLASS	"/sys/class/remoteproc"
/* Large enough for any full sysfs path including d_name (up to 255). */
#define RPROC_PATH_MAX		512
#define RPROC_ATTR_BUF_MAX	32
#define RPROC_MAX_DEVS		32

/*
 * Scan /sys/class/remoteproc/ and fill @devs with the full sysfs path of
 * each remoteprocN entry (e.g. "/sys/class/remoteproc/remoteproc0").
 * Returns the number of devices found, or 0 if none.
 */
static inline int rproc_find_devices(char devs[][RPROC_PATH_MAX], int max)
{
	struct dirent *entry;
	DIR *dir;
	int count = 0;

	dir = opendir(RPROC_SYSFS_CLASS);
	if (!dir)
		return 0;

	while ((entry = readdir(dir)) && count < max) {
		if (strncmp(entry->d_name, "remoteproc", 10) != 0)
			continue;
		snprintf(devs[count], RPROC_PATH_MAX, "%s/%s",
			 RPROC_SYSFS_CLASS, entry->d_name);
		count++;
	}

	closedir(dir);
	return count;
}

/*
 * Read sysfs attribute @attr of device at sysfs path @dev into @buf.
 * Strips trailing newline. Returns 0 on success, -errno on failure.
 */
static inline int rproc_sysfs_read(const char *dev, const char *attr,
				   char *buf, size_t len)
{
	char path[RPROC_PATH_MAX];
	ssize_t n;
	int fd;

	snprintf(path, sizeof(path), "%s/%s", dev, attr);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;

	n = read(fd, buf, len - 1);
	close(fd);
	if (n < 0)
		return -errno;

	buf[n] = '\0';
	if (n > 0 && buf[n - 1] == '\n')
		buf[n - 1] = '\0';

	return 0;
}

/*
 * Write @val to sysfs attribute @attr of device at sysfs path @dev.
 * Returns 0 on success, -errno on failure.
 */
static inline int rproc_sysfs_write(const char *dev, const char *attr,
				    const char *val)
{
	char path[RPROC_PATH_MAX];
	ssize_t n;
	int fd;

	snprintf(path, sizeof(path), "%s/%s", dev, attr);
	fd = open(path, O_WRONLY);
	if (fd < 0)
		return -errno;

	n = write(fd, val, strlen(val));
	close(fd);
	if (n < 0)
		return -errno;

	return 0;
}

#endif /* REMOTEPROC_HELPERS_H */
