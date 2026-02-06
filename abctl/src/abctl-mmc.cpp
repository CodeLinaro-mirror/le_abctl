/*
 * ---------------------------------------------------------------------------
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * ---------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <ctype.h>

#include "abctl-mmc.h"

int read_extcsd(int fd, __u8 *ext_csd)
{
    int ret = 0;
    struct mmc_ioc_cmd read_cmd;

    memset(&read_cmd, 0, sizeof(read_cmd));
    memset(ext_csd, 0, sizeof(__u8) * 512);
    read_cmd.write_flag = 0;
    read_cmd.opcode = ABCTL_MMC_SEND_EXT_CSD;
    read_cmd.arg = 0;
    read_cmd.flags = ABCTL_MMC_RSP_SPI_R1 | ABCTL_MMC_RSP_R1 | ABCTL_MMC_CMD_ADTC;
    read_cmd.blksz = 512;
    read_cmd.blocks = 1;
    mmc_ioc_cmd_set_data(read_cmd, ext_csd);

    ret = ioctl(fd, MMC_IOC_CMD, &read_cmd);
    if (ret)
        perror("ioctl");

    return ret;
}

int write_extcsd_value(int fd, __u8 index, __u8 value)
{
    int ret = 0;
    struct mmc_ioc_cmd write_cmd;

    memset(&write_cmd, 0, sizeof(write_cmd));
    write_cmd.write_flag = 1;
    write_cmd.opcode = ABCTL_MMC_SWITCH;
    write_cmd.arg = (ABCTL_MMC_SWITCH_MODE_WRITE_BYTE << 24) |
            (index << 16) |
            (value << 8) |
            ABCTL_EXT_CSD_CMD_SET_NORMAL;
            write_cmd.flags = ABCTL_MMC_RSP_SPI_R1B | ABCTL_MMC_RSP_R1B | ABCTL_MMC_CMD_AC;

    ret = ioctl(fd, MMC_IOC_CMD, &write_cmd);
    if (ret)
        perror("ioctl");

    return ret;
}

int do_write_boot_en(int boot_partition, int send_ack)
{
    __u8 ext_csd[512];
    __u8 value = 0;
    int fd, ret;
    const char *device = "/dev/mmcblk0"; // Hardcoded device path

    // Open the MMC device
    fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // Read EXT_CSD from the device
    ret = read_extcsd(fd, ext_csd);
    if (ret) {
        fprintf(stderr, "Could not read EXT_CSD from %s\n", device);
        return 1;
    }

    value = ext_csd[ABCTL_EXT_CSD_PART_CONFIG];

    // Configure the boot partition
    switch (boot_partition) {
        case ABCTL_EXT_CSD_PART_CONFIG_ACC_BOOT0:
            value |= (1 << 3);
            value &= ~(3 << 4);
            break;
        case ABCTL_EXT_CSD_PART_CONFIG_ACC_BOOT1:
            value |= (1 << 4);
            value &= ~(1 << 3);
            value &= ~(1 << 5);
            break;
        case ABCTL_EXT_CSD_PART_CONFIG_ACC_USER_AREA:
            value |= (boot_partition << 3);
            break;
        default:
            fprintf(stderr, "Cannot enable the boot area\n");
            return 1;
    }

    // Configure the acknowledgment bit
    if (send_ack)
        value |= ABCTL_EXT_CSD_PART_CONFIG_ACC_ACK;
    else
        value &= ~ABCTL_EXT_CSD_PART_CONFIG_ACC_ACK;

    // Write the modified value back to the device
    ret = write_extcsd_value(fd, ABCTL_EXT_CSD_PART_CONFIG, value);
    if (ret) {
        fprintf(stderr, "%s: Could not write 0x%02x to EXT_CSD[%d] in %s\n",
            __func__, value, ABCTL_EXT_CSD_PART_CONFIG, device);
        return 1;
    }

    fprintf(stderr, "%s: Successfully write 0x%02x to EXT_CSD[%d] in %s\n",
        __func__, value, ABCTL_EXT_CSD_PART_CONFIG, device);
    return 0;
}
