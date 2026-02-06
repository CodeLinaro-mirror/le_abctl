/*
 * ---------------------------------------------------------------------------
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * ---------------------------------------------------------------------------
 *
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "abctl-lvmisc.h"

static char lvmisc_dev_node[MAX_PATH];  // Pre-allocate a static array for lvmisc_dev_node

static int get_lvmisc_partition() {
    char resolved_path[MAX_PATH];
    // Read the symbolic link
    ssize_t len = readlink(LVMISC_SYMLINK_PATH, resolved_path, sizeof(resolved_path) - 1);
    if (len == -1) {
        fprintf(stderr, "%s: failed to readlink of %s\n", __func__, LVMISC_SYMLINK_PATH);
        return -1;
    }
    resolved_path[len] = '\0';

    // Resolve the path to the absolute path
    char *real_path = realpath(LVMISC_SYMLINK_PATH, NULL);
    if (real_path == NULL) {
        fprintf(stderr, "%s: failed to get realpath of: %s\n", __func__, LVMISC_SYMLINK_PATH);
        return -1;
    }

    // Copy the resolved real path to lvmisc_dev_node
    if (snprintf(lvmisc_dev_node, MAX_PATH, "%s", real_path) >= MAX_PATH) {
        fprintf(stderr, "%s: the real path of lvmisc dev node is too long\n", __func__);
        free(real_path);
        return -1;
    }

    // Free the memory allocated by realpath
    free(real_path);
    return 0;
}

int lvmisc_read(lvmisc_partition_t &lvmisc_partition)
{
    int ret = 0;

    do {
        ret = get_lvmisc_partition();
        if (ret == -1) {
            fprintf(stderr,"%s: failed to get lv_misc recovery partition\n", __func__);
            ret = -1;
            break;
        }

        int fd = open(lvmisc_dev_node, O_RDONLY | O_SYNC);
        if (-1 == fd) {
            fprintf(stderr, "%s: open() failed for %s\n", __func__, lvmisc_dev_node);
            ret = -1;
            break;
        }

        ssize_t read_bytes;
        do {
            do {
                read_bytes = read(fd, &lvmisc_partition, sizeof(lvmisc_partition_t));
            } while (((-1 == read_bytes) && (EINTR == errno))
                        || ((-1 != read_bytes) && (read_bytes != sizeof(lvmisc_partition_t))));

            if (-1 == read_bytes) {
                ret = -1;
                break;
            } else {
                ret = read_bytes;
            }
        } while (0);

        close(fd);

        if (ret == -1) {
            fprintf(stderr, "%s: Failed to read partition : %s\n", __func__, lvmisc_dev_node);
            ret = -1;
            break;
        }

    } while (0);

    return ret;
}

int lvmisc_write(lvmisc_partition_t &lvmisc_partition)
{
    ssize_t written_bytes = 0;
    int ret = -1;

    int fd = open(lvmisc_dev_node, O_RDWR | O_SYNC);
    if (-1 == fd) {
        fprintf(stderr, "%s: open() failed for %s\n", __func__, lvmisc_dev_node);
        ret = -1;
    } else {
        do {
            written_bytes = write(fd, &lvmisc_partition, sizeof(lvmisc_partition_t));
        } while ((-1 == written_bytes) && (EINTR == errno)
                    || ((-1 != written_bytes) && (written_bytes != sizeof(lvmisc_partition_t))));

        if ((-1 == written_bytes) || (written_bytes < sizeof(lvmisc_partition_t))) {
            ret = -1;
        } else {
            ret = written_bytes;
        }

        close(fd);
    }

    return ret;
}

void lvmisc_print_lvmisc(lvmisc_partition_t &lvmisc_partition) {

    if (lvmisc_read(lvmisc_partition) == -1) {
        fprintf(stderr,"%s: failed to read partition", __func__);
    }

    printf("lvmisc_partition.reserved[0]: gvm current_slot: %c \n", lvmisc_partition.reserved[0]);
    printf("lvmisc_partition.reserved[1]: gvm target_slot: %c \n", lvmisc_partition.reserved[1]);
    printf("lvmisc_partition.reserved[2]: gvm bootable status of target slot: %c \n", lvmisc_partition.reserved[2]);
    printf("lvmisc_partition.reserved[3]: gvm Slot0 Mark boot successful status: %c \n", lvmisc_partition.reserved[3]);
    printf("lvmisc_partition.reserved[4]: gvm Slot0 Set unBootable Status: %c \n", lvmisc_partition.reserved[4]);
    printf("lvmisc_partition.reserved[5]: gvm Slot1 Mark boot successful status: %c \n", lvmisc_partition.reserved[5]);
    printf("lvmisc_partition.reserved[6]: gvm Slot1 Set unBootable Status: %c \n", lvmisc_partition.reserved[6]);
    printf("lvmisc_partition.reserved[7]: slot_switch_config: %c \n", lvmisc_partition.reserved[7]);
}