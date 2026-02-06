/*
 * ---------------------------------------------------------------------------
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * ---------------------------------------------------------------------------
 */

#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>

#define MAX_SLOTS (2)
#define RECOVERYINFO_DEV_NODE "/dev/disk/by-partlabel/recoveryinfo"
#define MAX_PATH 1024

/* Common image set flags and owners */
#define SET_AB_USABLE                 (0x0UL)
#define DONT_USE_SET_A                (0x1UL)
#define DONT_USE_SET_B                (0x2UL)
#define DONT_USE_SET_AB               (0x3UL)

#define OWNER_XBL                     (1)
#define OWNER_HLOS                    (2)
#define OWNER_UEFI                    (3)
#define OWNER_ABL                     (4)

#define RECOVERY_BOOT_DATA_OFFSET 0
#define RECOVERY_BOOT_DATA_SIZE   sizeof(boot_recovery_boot_data_t)

#define FLAG_UPDATE_MASK 0
#define FLAG_UPDATE (0x01<<FLAG_UPDATE_MASK)

/* Place misc structure at 2KB offset in the 4KB recoveryinfo partition */
#define MISC_RECOVERY_INFO_OFFSET     2048
#define MISC_DATA_SIZE                sizeof(recoveryinfo_misc_t)

#define RESERVED_FIELD_SIZE     16

/* ========================= misc struct definitions ========================= */
typedef struct __attribute__((packed))
{
    uint32_t update_flag;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
    uint8_t  reserved4[RESERVED_FIELD_SIZE];
    uint8_t  reserved5[RESERVED_FIELD_SIZE];
} recoveryinfo_misc_t;

/* ---------------------------------------------------------------------------
 * Macro-gated structures:
 *  - RECOVERYINFO_V1: Nord HGY: use Recovery Boot Info + Trial Boot Info + Misc
 *  - else: lemans HGY: use Misc only (keep current recoveryinfo_misc_t)
 * --------------------------------------------------------------------------- */
#ifdef RECOVERYINFO_V1
/* ========================= NordHGY definitions ========================= */

/* Magic cookies for accessing RBI */
#define BOOT_RBI_MAGIC_COOKIE            0x72637279UL     /* 'rcry' */
#define BOOT_RBI_BOOT_SET_MAGIC          0x6D62U          /* 'mb' */
#define BOOT_RBI_CRC32_DEFAULT_SEED      0xFFFFFFFFUL
#define BOOT_RBI_CRC_MAGIC               0x72637263UL     /* 'rcrc' */

typedef struct  __attribute__((packed))
{
  uint16_t magic;
  uint16_t selected_set;
} _boot_rbi_boot_set_t;


typedef struct  __attribute__((packed))
{
  uint32_t magic_cookie;
  uint32_t version;
  uint32_t image_set_status;
  uint32_t owner;
  _boot_rbi_boot_set_t boot_set;      /* uint32 */
  uint32_t reserved1;
  uint8_t  reserved2 [RESERVED_FIELD_SIZE];
  uint8_t  reserved3 [RESERVED_FIELD_SIZE];
  uint32_t reserved4;
  uint32_t reserved5;
  uint32_t reserved6;
  uint32_t reserved7;
  uint32_t reserved8;
  uint32_t reserved9;
  uint32_t reserved10;
  uint32_t reserved11;

  /* CRC fields for fields above */
  uint32_t recovery_boot_info_crc;
  uint32_t recovery_boot_info_crc_enable_magic;  /* rcrc */
} boot_recovery_boot_data_t;


/* Trial Boot Info data*/
#define TRIAL_BOOT_DATA_OFFSET         (RECOVERY_BOOT_DATA_OFFSET + RECOVERY_BOOT_DATA_SIZE)
#define TRIAL_BOOT_DATA_SIZE           sizeof(boot_trial_boot_data_t)

#define BOOT_TBI_MAGIC_COOKIE                       0x74726269 /* 'trbi' */
#define BOOT_TBI_VERSION                            0x2

#define BOOT_TBI_MAX_ATTEMPTS                       6+1 /* 6: Total number of trial boots, +1: as boot count starts from 1 */

#define BOOT_TBI_ENABLE_TRIAL_BOOT                  0x1
#define BOOT_TBI_ENABLE_PARTITION_TABLE_RESTORE     0x1

#define BOOT_TBI_CRC32_DEFAULT_SEED                 0xFFFFFFFFUL
#define BOOT_TBI_CRC_MAGIC                          0x74637263UL   /* 'tcrc' */

typedef struct __attribute__((packed))
{
  uint32_t magic_cookie;
  uint32_t version;
  uint32_t trial_boot_enable;
  uint32_t trial_boot_max_attempts;
  uint32_t trial_boot_failed_attempts;
  uint32_t partition_table_restore_enable;
  uint32_t partition_table_restore_status;
  uint32_t reserved1;
  uint32_t image_set_to_boot;
  uint32_t enable_edl_feature_for_rollback_failed;
  uint32_t trial_boot_rollback_failed_attempts;
 
  /* CRC fields for fields above */
  uint32_t trial_boot_info_crc;
  uint32_t trial_boot_info_crc_enable_magic;
} boot_trial_boot_data_t;

typedef struct {
    boot_recovery_boot_data_t recovery_boot_data;
    boot_trial_boot_data_t    trial_boot_data;
    recoveryinfo_misc_t       misc_data;
} recoveryinfo_full_t;

/* ========================= lemansHGY definitions ========================= */
#else
typedef struct __attribute__((packed))
{
  uint16_t magic;
  uint16_t boot_count;
} _recovery_info_xbl_retry_data_t;

typedef struct  __attribute__((packed))
{
  uint32_t magic_cookie;
  uint32_t version;
  uint32_t image_set_status;
  uint32_t owner;
  uint32_t reserve;
  _recovery_info_xbl_retry_data_t xbl_retry_data;
  uint8_t  corrupt_guid_bank_a[RESERVED_FIELD_SIZE];
  uint8_t  corrupt_guid_bank_b[RESERVED_FIELD_SIZE];
} boot_recovery_boot_data_t;

typedef struct {
    boot_recovery_boot_data_t recovery_boot_data;
    recoveryinfo_misc_t       misc_data;
} recoveryinfo_full_t;

#endif /* macro gating */

/* ----------------- RecoveryInfo APIs ------------------------- */
int recoveryinfo_setUpdate();
int recoveryinfo_clearUpdate();
void recoveryinfo_printRecoveryinfo();
int recoveryinfo_clearImgSetStatus(unsigned int slot);
