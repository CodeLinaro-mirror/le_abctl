/*
 * ---------------------------------------------------------------------------
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * ---------------------------------------------------------------------------
 */

#ifndef __LVMISC_PARTITION_H_
#define __LVMISC_PARTITION_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/fs.h>
#include <stdlib.h>


#define MAX_SLOTS (2)
#define LVMISC_SYMLINK_PATH "/dev/block/platform/lv_misc"
#define MAX_PATH 1024

typedef uint8_t devtype_t;

/* ----------------- lv_misc Partition Layout ------------------------- */
/*
    lvmisc_partition.reserved[0]: gvm current_slot  (expected ‘a’ or ‘b’)
    lvmisc_partition.reserved[1]: gvm target_slot   (expected ‘a’ or ‘b’)
    lvmisc_partition.reserved[2]: gvm bootable status of target slot  (‘y’ or ‘n’)
    lvmisc_partition.reserved[3]: gvm Slot0 Mark boot successful status (‘y’ or ‘n’)
    lvmisc_partition.reserved[4]: gvm Slot0 Set unBootable Status  (‘y’ or ‘n’)
    lvmisc_partition.reserved[5]: gvm Slot1 Mark boot successful status (‘y’ or ‘n’)
    lvmisc_partition.reserved[6]: gvm Slot1 Set unBootable Status  (‘y’ or ‘n’)
    lvmisc_partition.reserved[7]: slot_switch_config

    Meaning: current_slot = a, target_slot = a, Slot0 Mark boot successful status=y, Slot0 Set Slot Bootable Status = y
*/
typedef struct bootloader_message {
  // lvmisc data structure keeps consistent with lamisc
  char command[32];
  char status[32];
  char recovery[768];
  char stage[32];
  char reserved[1184];
} lvmisc_partition_t;

int lvmisc_read(lvmisc_partition_t &lvmisc_partition);
int lvmisc_write(lvmisc_partition_t &lvmisc_partition);
void lvmisc_print_lvmisc(lvmisc_partition_t &lvmisc_partition);

#endif
