/*
 * ---------------------------------------------------------------------------
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * ---------------------------------------------------------------------------
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "libabctl.h"
#include "abctl-recoveryinfo.h"

static char recoveryinfo_dev_node[MAX_PATH] = {0};

/* ---------------------------------------------------------------------------
 * Get recoveryinfo partition path
 * --------------------------------------------------------------------------- */
static int get_recoveryinfo_partition() {
    char resolved_path[MAX_PATH];
    // Read the symbolic link
    ssize_t len = readlink(RECOVERYINFO_DEV_NODE, resolved_path, sizeof(resolved_path) - 1);
    if (len == -1) {
        fprintf(stderr, "%s: failed to readlink of %s\n", __func__, RECOVERYINFO_DEV_NODE);
        return -1;
    }
    resolved_path[len] = '\0';

    // Resolve the path to the absolute path
    char *real_path = realpath(RECOVERYINFO_DEV_NODE, NULL);
    if (real_path == NULL) {
        fprintf(stderr, "%s: failed to get realpath of: %s\n", __func__, RECOVERYINFO_DEV_NODE);
        return -1;
    }

    // Copy the resolved real path to recoveryinfo_dev_node
    if (snprintf(recoveryinfo_dev_node, MAX_PATH, "%s", real_path) >= MAX_PATH) {
        fprintf(stderr, "%s: the real path of recoveryinfo dev node is too long\n", __func__);
        free(real_path);
        return -1;
    }

    // Free the memory allocated by realpath
    free(real_path);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Recovery Boot Data read/write functions (offset RECOVERY_BOOT_DATA_OFFSET = 0)
 * --------------------------------------------------------------------------- */
static int recoveryinfo_read_rci(boot_recovery_boot_data_t &recovery_boot_data)
{
    ssize_t read_bytes;
    int ret = -1;

    if (strlen(recoveryinfo_dev_node) == 0) {
        if (get_recoveryinfo_partition() != 0) {
            fprintf(stderr, "%s: failed to get recoveryinfo partition\n", __func__);
            return -1;
        }
    }

    int fd = open(recoveryinfo_dev_node, O_RDONLY | O_SYNC);
    if (-1 == fd) {
        fprintf(stderr, "%s: open() failed for %s\n", __func__, recoveryinfo_dev_node);
        return -1;
    }

    do {
        lseek(fd, RECOVERY_BOOT_DATA_OFFSET, SEEK_SET);
        do {
            read_bytes = read(fd, &recovery_boot_data, RECOVERY_BOOT_DATA_SIZE);
        } while (((-1 == read_bytes) && (EINTR == errno))
                    || ((-1 != read_bytes) && (read_bytes != RECOVERY_BOOT_DATA_SIZE)));

        if (-1 == read_bytes) {
            ret = -1;
            break;
        }

        ret = read_bytes;
    } while(0);

    close(fd);
    return ret;
}

static int recoveryinfo_write_rci(boot_recovery_boot_data_t &recovery_boot_data)
{
    ssize_t written_bytes;
    int ret = -1;

    if (strlen(recoveryinfo_dev_node) == 0) {
        if (get_recoveryinfo_partition() != 0) {
            fprintf(stderr, "%s: failed to get recoveryinfo partition\n", __func__);
            return -1;
        }
    }

    int fd = open(recoveryinfo_dev_node, O_RDWR | O_SYNC);
    if (-1 == fd) {
        fprintf(stderr, "%s: open() failed for %s\n", __func__, recoveryinfo_dev_node);
        return -1;
    }

    do {
        lseek(fd, RECOVERY_BOOT_DATA_OFFSET, SEEK_SET);
        written_bytes = write(fd, &recovery_boot_data, RECOVERY_BOOT_DATA_SIZE);
    } while ((-1 == written_bytes) && (EINTR == errno)
                || ((-1 != written_bytes) && (written_bytes != RECOVERY_BOOT_DATA_SIZE)));

    if (-1 == written_bytes) {
        ret = -1;
    } else {
        ret = written_bytes;
    }

    close(fd);
    return ret;
}

#ifdef RECOVERYINFO_V1
/* ---------------------------------------------------------------------------
 * Trial Boot Data read/write functions (offset TRIAL_BOOT_DATA_OFFSET)
 * --------------------------------------------------------------------------- */
static int recoveryinfo_read_tbi(boot_trial_boot_data_t &trial_boot_data)
{
    ssize_t read_bytes;
    int ret = -1;

    if (strlen(recoveryinfo_dev_node) == 0) {
        if (get_recoveryinfo_partition() != 0) {
            fprintf(stderr, "%s: failed to get recoveryinfo partition\n", __func__);
            return -1;
        }
    }

    int fd = open(recoveryinfo_dev_node, O_RDONLY | O_SYNC);
    if (-1 == fd) {
        fprintf(stderr, "%s: open() failed for %s\n", __func__, recoveryinfo_dev_node);
        return -1;
    }

    do {
        lseek(fd, TRIAL_BOOT_DATA_OFFSET, SEEK_SET);
        do {
            read_bytes = read(fd, &trial_boot_data, TRIAL_BOOT_DATA_SIZE);
        } while (((-1 == read_bytes) && (EINTR == errno))
                    || ((-1 != read_bytes) && (read_bytes != TRIAL_BOOT_DATA_SIZE)));

        if (-1 == read_bytes) {
            ret = -1;
            break;
        }

        ret = read_bytes;
    } while(0);

    close(fd);
    return ret;
}

static int recoveryinfo_write_tbi(boot_trial_boot_data_t &trial_boot_data)
{
    ssize_t written_bytes;
    int ret = -1;

    if (strlen(recoveryinfo_dev_node) == 0) {
        if (get_recoveryinfo_partition() != 0) {
            fprintf(stderr, "%s: failed to get recoveryinfo partition\n", __func__);
            return -1;
        }
    }

    int fd = open(recoveryinfo_dev_node, O_RDWR | O_SYNC);
    if (-1 == fd) {
        fprintf(stderr, "%s: open() failed for %s\n", __func__, recoveryinfo_dev_node);
        return -1;
    }

    do {
        lseek(fd, TRIAL_BOOT_DATA_OFFSET, SEEK_SET);
        written_bytes = write(fd, &trial_boot_data, TRIAL_BOOT_DATA_SIZE);
    } while ((-1 == written_bytes) && (EINTR == errno)
                || ((-1 != written_bytes) && (written_bytes != TRIAL_BOOT_DATA_SIZE)));

    if (-1 == written_bytes) {
        ret = -1;
    } else {
        ret = written_bytes;
    }

    close(fd);
    return ret;
}
#endif

/* ---------------------------------------------------------------------------
 * Misc Data read/write functions (offset MISC_RECOVERY_INFO_OFFSET = 2048)
 * --------------------------------------------------------------------------- */
static int recoveryinfo_read_misc(recoveryinfo_misc_t &misc_recoveryinfo)
{
    ssize_t read_bytes;
    int ret = -1;

    if (strlen(recoveryinfo_dev_node) == 0) {
        if (get_recoveryinfo_partition() != 0) {
            fprintf(stderr, "%s: failed to get recoveryinfo partition\n", __func__);
            return -1;
        }
    }

    int fd = open(recoveryinfo_dev_node, O_RDONLY | O_SYNC);
    if (-1 == fd) {
        fprintf(stderr, "%s: open() failed for %s\n", __func__, recoveryinfo_dev_node);
        return -1;
    }

    do {
        lseek(fd, MISC_RECOVERY_INFO_OFFSET, SEEK_SET);
        do {
            read_bytes = read(fd, &misc_recoveryinfo, MISC_DATA_SIZE);
        } while (((-1 == read_bytes) && (EINTR == errno))
                    || ((-1 != read_bytes) && (read_bytes != MISC_DATA_SIZE)));

        if (-1 == read_bytes) {
            ret = -1;
            break;
        } else {
            ret = read_bytes;
        }
    } while(0);

    close(fd);
    return ret;
}

static int recoveryinfo_write_misc(recoveryinfo_misc_t &misc_recoveryinfo)
{
    ssize_t written_bytes;
    int ret = -1;

    if (strlen(recoveryinfo_dev_node) == 0) {
        if (get_recoveryinfo_partition() != 0) {
            fprintf(stderr, "%s: failed to get recoveryinfo partition\n", __func__);
            return -1;
        }
    }

    int fd = open(recoveryinfo_dev_node, O_RDWR | O_SYNC);
    if (-1 == fd) {
        fprintf(stderr, "%s: open() failed for %s\n", __func__, recoveryinfo_dev_node);
        return -1;
    }

    do {
        lseek(fd, MISC_RECOVERY_INFO_OFFSET, SEEK_SET);
        written_bytes = write(fd, &misc_recoveryinfo, MISC_DATA_SIZE);
    } while ((-1 == written_bytes) && (EINTR == errno)
                || ((-1 != written_bytes) && (written_bytes != MISC_DATA_SIZE)));

    if (-1 == written_bytes) {
        ret = -1;
    } else {
        ret = written_bytes;
    }

    close(fd);
    return ret;
}

/* ---------------------------------------------------------------------------
 * Public API implementation
 * --------------------------------------------------------------------------- */
int recoveryinfo_setUpdate()
{
    int ret = -1;
    recoveryinfo_misc_t misc_recoveryinfo;
    memset(&misc_recoveryinfo, 0, MISC_DATA_SIZE);

    do {
        // read misc structure
        ret = recoveryinfo_read_misc(misc_recoveryinfo);
        if (ret == -1) {
            fprintf(stderr, "%s: failed to read misc at offset %d\n", __func__, MISC_RECOVERY_INFO_OFFSET);
            memset(&misc_recoveryinfo, 0, MISC_DATA_SIZE);
        }

        /* Set update flag in misc structure */
        misc_recoveryinfo.update_flag |= FLAG_UPDATE;

        /* Write misc structure back */
        ret = recoveryinfo_write_misc(misc_recoveryinfo);
        if (ret == -1) {
            fprintf(stderr, "%s: Failed to write misc partition : %s\n", __func__, recoveryinfo_dev_node);
            ret = -1;
            break;
        }

    } while(0);

    return ret;
}

int recoveryinfo_clearUpdate()
{
    int ret = -1;
    recoveryinfo_misc_t misc_recoveryinfo;
    memset(&misc_recoveryinfo, 0, MISC_DATA_SIZE);

    do {
        // read misc structure
        ret = recoveryinfo_read_misc(misc_recoveryinfo);
        if (ret == -1) {
            fprintf(stderr, "%s: failed to read misc at offset %d\n", __func__, MISC_RECOVERY_INFO_OFFSET);
            memset(&misc_recoveryinfo, 0, MISC_DATA_SIZE);
        }

        /* Clear update flag in misc structure */
        misc_recoveryinfo.update_flag &= ~FLAG_UPDATE;

        /* Write misc structure back */
        ret = recoveryinfo_write_misc(misc_recoveryinfo);
        if (ret == -1) {
            fprintf(stderr, "%s: Failed to write misc partition : %s\n", __func__, recoveryinfo_dev_node);
            ret = -1;
            break;
        }

    } while(0);

    return ret;
}

void recoveryinfo_printRecoveryinfo() {
    boot_recovery_boot_data_t recovery_boot_data;
    recoveryinfo_misc_t misc_recoveryinfo;

    memset(&recovery_boot_data, 0, sizeof(recovery_boot_data));
    memset(&misc_recoveryinfo, 0, MISC_DATA_SIZE);

    printf("\n========== Recovery Info Partition Data ==========\n");

    // Read and print Recovery Boot Info
    if (recoveryinfo_read_rci(recovery_boot_data) != -1) {
        printf("\n=== Recovery Boot Info ===\n");
        printf("magic_cookie                    : 0x%X \n", recovery_boot_data.magic_cookie);
        printf("version                         : 0x%X \n", recovery_boot_data.version);
        printf("image_set_status                : 0x%X \n", recovery_boot_data.image_set_status);
        printf("owner                           : 0x%X \n", recovery_boot_data.owner);
    } else {
        printf("\n=== Recovery Boot Info === [READ FAILED]\n");
    }

    // Read and print Misc Info
    if (recoveryinfo_read_misc(misc_recoveryinfo) != -1) {
        printf("\n=== Misc Info ===\n");
        printf("misc.update_flag                                   : 0x%X \n", misc_recoveryinfo.update_flag);
        printf("misc.reserved1                                     : 0x%X \n", misc_recoveryinfo.reserved1);
        printf("misc.reserved2                                     : 0x%X \n", misc_recoveryinfo.reserved2);
        printf("misc.reserved3                                     : 0x%X \n", misc_recoveryinfo.reserved3);
    } else {
        printf("\n=== Misc Info === [READ FAILED]\n");
    }

    printf("\n================================================\n");
}

int recoveryinfo_clearImgSetStatus(unsigned int slot)
{
    int ret = -1;
    boot_recovery_boot_data_t recovery_boot_data;

    memset(&recovery_boot_data, 0, sizeof(recovery_boot_data));
    assert((slot < MAX_SLOTS) && (slot >= 0));

    do {
        // Read current recovery boot data
        ret = recoveryinfo_read_rci(recovery_boot_data);
        if (ret == -1) {
            fprintf(stderr, "%s: failed to read recovery boot data\n", __func__);
            break;
        }

        // Clear unbootable flag for the specified slot
        int unbootable_slot[MAX_SLOTS] = {DONT_USE_SET_A, DONT_USE_SET_B};
        recovery_boot_data.image_set_status &= ~(unbootable_slot[slot]); // clear unbootable flag
        recovery_boot_data.owner = OWNER_HLOS;

        // Write back the updated data
        ret = recoveryinfo_write_rci(recovery_boot_data);
        if (ret == -1) {
            fprintf(stderr, "%s: Failed to write recovery boot data\n", __func__);
            break;
        }

    } while(0);

    return ret;
}
