/*
 * ---------------------------------------------------------------------------
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * ---------------------------------------------------------------------------
 */

#include <asm-generic/int-ll64.h>
#include <linux/mmc/ioctl.h>
#include <linux/major.h>
#include <stdio.h>

/* From kernel linux/mmc/mmc.h */
#define ABCTL_MMC_SWITCH          6	/* ac	[31:0] See below	R1b */
#define ABCTL_MMC_SEND_EXT_CSD    8	/* adtc				R1  */
#define ABCTL_MMC_SWITCH_MODE_WRITE_BYTE	0x03	/* Set target to value */

/* EXT_CSD fields  */
#define ABCTL_EXT_CSD_PART_CONFIG		179

/* EXT_CSD field definitions  */
#define ABCTL_EXT_CSD_CMD_SET_NORMAL     (1<<0)
#define ABCTL_EXT_CSD_PART_CONFIG_ACC_BOOT0      (0x1)
#define ABCTL_EXT_CSD_PART_CONFIG_ACC_BOOT1      (0x2)
#define ABCTL_EXT_CSD_PART_CONFIG_ACC_USER_AREA  (0x7)
#define ABCTL_EXT_CSD_PART_CONFIG_ACC_ACK        (0x40)

/* From kernel linux/mmc/core.h */
#define ABCTL_MMC_RSP_PRESENT	(1 << 0)
#define ABCTL_MMC_RSP_CRC     (1 << 2)		/* expect valid crc */
#define ABCTL_MMC_RSP_BUSY	(1 << 3)		/* card may send busy */
#define ABCTL_MMC_RSP_OPCODE	(1 << 4)		/* response contains opcode */

#define ABCTL_MMC_CMD_AC     (0 << 5)
#define ABCTL_MMC_CMD_ADTC   (1 << 5)

#define ABCTL_MMC_RSP_SPI_S1      (1 << 7)		/* one status byte */
#define ABCTL_MMC_RSP_SPI_BUSY    (1 << 10)		/* card may send busy */

#define ABCTL_MMC_RSP_SPI_R1      (ABCTL_MMC_RSP_SPI_S1)
#define ABCTL_MMC_RSP_SPI_R1B     (ABCTL_MMC_RSP_SPI_S1|ABCTL_MMC_RSP_SPI_BUSY)

#define ABCTL_MMC_RSP_R1     (ABCTL_MMC_RSP_PRESENT|ABCTL_MMC_RSP_CRC|ABCTL_MMC_RSP_OPCODE)
#define ABCTL_MMC_RSP_R1B    (ABCTL_MMC_RSP_PRESENT|ABCTL_MMC_RSP_CRC|ABCTL_MMC_RSP_OPCODE|ABCTL_MMC_RSP_BUSY)

int do_write_boot_en(int boot_partition, int send_ack);
