// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <gpt.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include "abctl-ufs-bsg.h"

#define XBL_AB_PRIMARY      "/dev/disk/by-partlabel/xbl_a"
#define XBL_AB_SECONDARY    "/dev/disk/by-partlabel/xbl_b"
#define CMDLINE_BOOT_DEV    "androidboot.bootdevice="

//boot device type
#define TYPE_ERROR   -1
#define TYPE_EMMC    0
#define TYPE_UFS     1

//This will allow us to get the root lun path from the path to the partition.
//i.e: from /dev/sdaXXX get /dev/sda.
#define PATH_TRUNCATE_LOC (sizeof("/dev/sda") - 1)
#define LUN_NAME_START_LOC (sizeof("/dev/") - 1)

#define BOOT_LUN_A_ID 1
#define BOOT_LUN_B_ID 2

#define SLOT_SUFFIX_STR      ("systemd.setenv=\"SLOT_SUFFIX=_")
#define SLOT_SUFFIX_STRLEN   (sizeof(SLOT_SUFFIX_STR) - sizeof(char))

#ifdef __USE_SIMULATION__
#define KERNEL_CMDLINE       ("/tmp/proc/cmdline")
#else
#define KERNEL_CMDLINE       ("/proc/cmdline")
#endif // ifdef __USE_SIMULATION__
#define CMDLINE_LEN          (2048)

#define PARTITION_ATTRIBUTE_PRIORITY_BIT_POS      (48)
#define PARTITION_ATTRIBUTE_ACTIVE_BIT_POS        (50)
#define PARTITION_ATTRIBUTE_MAX_RETRY_BIT_POS     (51)
#define PARTITION_ATTRIBUTE_SUCCESS_BIT_POS       (54)
#define PARTITION_ATTRIBUTE_UNBOOTABLE_BIT_POS    (55)

#define PARTITION_ATTRIBUTE_PRIORITY_VAL_MIN \
            ((uint64_t)(0x1) << PARTITION_ATTRIBUTE_PRIORITY_BIT_POS)
#define PARTITION_ATTRIBUTE_PRIORITY_VAL_MAX \
            ((uint64_t)(0x3) << PARTITION_ATTRIBUTE_PRIORITY_BIT_POS)
#define PARTITION_ATTRIBUTE_ACTIVE_VAL   \
            ((uint64_t)0x1 << PARTITION_ATTRIBUTE_ACTIVE_BIT_POS)
#define PARTITION_ATTRIBUTE_MAX_RETRY_COUNT_VAL \
            ((uint64_t)0x7 << PARTITION_ATTRIBUTE_MAX_RETRY_BIT_POS)
#define PARTITION_ATTRIBUTE_SUCCESSFUL_VAL \
            ((uint64_t)0x1 << PARTITION_ATTRIBUTE_SUCCESS_BIT_POS)
#define PARTITION_ATTRIBUTE_UNBOOTABLE_VAL \
            ((uint64_t)0x1 << PARTITION_ATTRIBUTE_UNBOOTABLE_BIT_POS)

#define PARTITION_ATTRIBUTE_PRIORITY_CLR   \
            (~((uint64_t)0x3 << PARTITION_ATTRIBUTE_PRIORITY_BIT_POS))
#define PARTITION_ATTRIBUTE_ACTIVE_CLR     \
            (~((uint64_t)0x1 << PARTITION_ATTRIBUTE_ACTIVE_BIT_POS))
#define PARTITION_ATTRIBUTE_MAX_RETRY_COUNT_CLR \
            (~((uint64_t)0x7 << PARTITION_ATTRIBUTE_MAX_RETRY_BIT_POS))
#define PARTITION_ATTRIBUTE_SUCCESSFUL_CLR \
            (~((uint64_t)0x1 << PARTITION_ATTRIBUTE_SUCCESS_BIT_POS))
#define PARTITION_ATTRIBUTE_UNBOOTABLE_CLR \
            (~((uint64_t)0x1 << PARTITION_ATTRIBUTE_UNBOOTABLE_BIT_POS))

#define STRLEN_CHAR16(x)     ((sizeof(x)/sizeof(char16_t)) - 1)

#define SBL_PREFIX        (u"sbl1_")
#define RPM_PREFIX        (u"rpm_")
#define TZ_PREFIX         (u"tz_")
#define ABOOT_PREFIX      (u"aboot_")
#define BOOT_PREFIX       (u"boot_")
#define MODEM_PREFIX      (u"modem_")
#define DSP_PREFIX        (u"dsp_")
#define SYSTEM_PREFIX     (u"system_")
#define DEVCFG_PREFIX     (u"devcfg_")
#define LKSECAPP_PREFIX   (u"lksecapp_")
#define CMNLIB_PREFIX     (u"cmnlib_")
#define CMNLIB64_PREFIX   (u"cmnlib64_")
#define KEYMASTER_PREFIX  (u"keymaster_")
#define ABL_PREFIX        (u"abl_")
#define AOP_PREFIX        (u"aop_")
#define AOP_CONFIG_PREFIX (u"aop_config_")
#define BLUETOOTH_PREFIX  (u"bluetooth_")
#define HYP_PREFIX        (u"hyp_")
#define QUPFW_PREFIX      (u"qupfw_")
#define STORSEC_PREFIX    (u"storsec_")
#define VBMETA_PREFIX     (u"vbmeta_")
#define VENDOR_PREFIX     (u"vendor_")
#define XBL_PREFIX        (u"xbl_")
#define XBLCONFIG_PREFIX  (u"xbl_config_")
#define MDTP_PREFIX       (u"mdtp_")
#define MDTPSECAPP_PREFIX (u"mdtpsecapp_")
#define IMAGEFV_PREFIX    (u"ImageFv_")

#define DTBO_PREFIX           (u"dtbo_")

#define UEFI_PREFIX           (u"uefi_")
#define UEFISECAPP_PREFIX     (u"uefisecapp_")
#define CORENHLOS_PREFIX      (u"core_nhlos_")
#define MULTIIMGOEM_PREFIX    (u"multiimgoem_")
#define MULTIIMGQTI_PREFIX    (u"multiimgqti_")
#define VMLINUX_PREFIX        (u"vm-linux_")
#define FEATENABLER_PREFIX    (u"featenabler_")

#define SHRM_PREFIX           (u"shrm_")
#define VENDOR_BOOT_PREFIX    (u"vendor_boot_")
#define VENDOR_DLKM_PREFIX    (u"vendor_dlkm_")
#define CPUCP_PREFIX          (u"cpucp_")

typedef struct {
   const char16_t *partition_name_prefix; /* Partition name prefix string. */
   unsigned short prefix_len;       /* Partition name prefix Length. */
   unsigned short uses_part_type_guid;   /* If '1', A/B control uses partition type GUID
                                            for A/B slot control. */
   guid_t active_slot_guid[16];     /* GUID for indicates active slot*/
   guid_t dormant_slot_guid[16];    /* GUID for non-active slot*/
   unsigned short ignore_if_missing;/* If '1', if partiton is missing in partition table
                                       then slot control implmentation will ignore the
                                       and move to next */
}  __attribute__((packed)) partition_guid_map_t;

/*
   This pre-populated part_guid_map is quite crummy.
   TODO:
    1. Use a run-time populated part_guid_map
    2. USe macros for the GUIDs
 */
partition_guid_map_t part_guid_map[] = {
   { SBL_PREFIX, STRLEN_CHAR16(SBL_PREFIX), 1,
     {0x2c,0xba,0xa0,0xde,0xdd,0xcb,0x05,0x48,0xb4,0xf9,0xf4,0x28,0x25,0x1c,0x3e,0x98},
     {0xa2,0xa0,0xd0,0xeb,0xe5,0xb9,0x33,0x44,0x87,0xc0,0x68,0xb6,0xb7,0x26,0x99,0xc7},
     1
   },
   { RPM_PREFIX, STRLEN_CHAR16(RPM_PREFIX), 1,
     {0x93,0xf7,0x8d,0x09,0x12,0xd7,0x3d,0x41,0x9d,0x4e,0x89,0xd7,0x11,0x77,0x22,0x28},
     {0xa2,0xa0,0xd0,0xeb,0xe5,0xb9,0x33,0x44,0x87,0xc0,0x68,0xb6,0xb7,0x26,0x99,0xc7},
     1
   },
   { TZ_PREFIX, STRLEN_CHAR16(TZ_PREFIX), 1,
     {0x7f,0xaa,0x53,0xa0,0xb8,0x40,0x1c,0x4b,0xba,0x08,0x2f,0x68,0xac,0x71,0xa4,0xf4},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { ABOOT_PREFIX, STRLEN_CHAR16(ABOOT_PREFIX), 1,
     {0xcd,0xfd,0x0f,0x40,0xe0,0x22,0xe7,0x47,0x9a,0x23,0xf1,0x6e,0xd9,0x38,0x23,0x88},
     {0xa2,0xa0,0xd0,0xeb,0xe5,0xb9,0x33,0x44,0x87,0xc0,0x68,0xb6,0xb7,0x26,0x99,0xc7},
     1
   },
   { BOOT_PREFIX,      STRLEN_CHAR16(BOOT_PREFIX), 1,
     {0x86,0x7f,0x11,0x20,0x85,0xe9,0x57,0x43,0xb9,0xee,0x37,0x4b,0xc1,0xd8,0x48,0x7d},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     0
   },
   { MODEM_PREFIX,     STRLEN_CHAR16(MODEM_PREFIX),     0,
     {0xa2,0xa0,0xd0,0xeb,0xe5,0xb9,0x33,0x44,0x87,0xc0,0x68,0xb6,0xb7,0x26,0x99,0xc7},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { DSP_PREFIX,       STRLEN_CHAR16(DSP_PREFIX),       0,
     {0x10,0x50,0xfe,0x7e,0x1a,0x2a,0x1a,0x4a,0xb8,0xbc,0x99,0x02,0x57,0x81,0x35,0x12},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { SYSTEM_PREFIX,    STRLEN_CHAR16(SYSTEM_PREFIX),    0,
     {0x11,0xb0,0xd7,0x97,0xda,0x54,0x35,0x48,0xb3,0xc4,0x91,0x7a,0xd6,0xe7,0x3d,0x74},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     0
   },
   { DEVCFG_PREFIX,    STRLEN_CHAR16(DEVCFG_PREFIX),    1,
     {0x16,0x4b,0x5d,0xf6,0x3d,0x34,0x25,0x4e,0xaa,0xfc,0xbe,0x99,0xb6,0x55,0x6a,0x6d},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { LKSECAPP_PREFIX,  STRLEN_CHAR16(LKSECAPP_PREFIX),  0, {0x0}, {0x0}, 1 },
   { CMNLIB_PREFIX,    STRLEN_CHAR16(CMNLIB_PREFIX),    1,
     {0x95,0x17,0x47,0x73,0x54,0xab,0xf9,0x43,0xa8,0x47,0x4f,0x72,0xea,0x5c,0xbe,0xf5},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { CMNLIB64_PREFIX,    STRLEN_CHAR16(CMNLIB64_PREFIX),1,
     {0x93,0x48,0xa6,0x8e,0x67,0x12,0x1b,0x4a,0x94,0x7c,0x7c,0x36,0x2a,0xca,0xad,0x2c},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { KEYMASTER_PREFIX, STRLEN_CHAR16(KEYMASTER_PREFIX), 1,
     {0x7c,0x2a,0x1d,0xa1,0x2a,0xd8,0x2f,0x4c,0x8a,0x01,0x18,0x05,0x24,0x0e,0x66,0x26},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { ABL_PREFIX,       STRLEN_CHAR16(ABL_PREFIX),       1,
     {0xa1,0x28,0x69,0xbd,0xe0,0x4c,0x38,0xa0,0x4f,0x3a,0x14,0x95,0xe3,0xed,0xdf,0xfb},
     {0x77,0xb0,0x14,0x41,0x5d,0x00,0x12,0x4e,0xac,0x8c,0xb4,0x93,0xbd,0xa6,0x84,0xfb},
     1
   },
   { AOP_PREFIX,       STRLEN_CHAR16(AOP_PREFIX),       1,
     {0xa5,0x90,0x9e,0xd6,0xab,0x4c,0x71,0x00,0xf6,0xdf,0xab,0x97,0x7f,0x14,0x1a,0x7f},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { AOP_CONFIG_PREFIX,       STRLEN_CHAR16(AOP_CONFIG_PREFIX),       1,
     {0x34,0xf2,0x12,0x3d,0x82,0xc8,0x46,0x4b,0xa2,0x0c,0x17,0xd5,0x2c,0x8f,0xc0,0x3d},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { BLUETOOTH_PREFIX, STRLEN_CHAR16(BLUETOOTH_PREFIX), 0,
     {0xf1,0x47,0xb7,0x6c,0xef,0xc2,0x92,0x40,0xad,0xd0,0xca,0x39,0xf7,0x9c,0x7a,0xf4},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { HYP_PREFIX,       STRLEN_CHAR16(HYP_PREFIX),       1,
     {0x89,0xa6,0xa6,0xe1,0x8d,0x0c,0xc6,0x4c,0xb4,0xe8,0x55,0xa4,0x32,0x0f,0xbd,0x8a},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { QUPFW_PREFIX,     STRLEN_CHAR16(QUPFW_PREFIX),     1,
     {0x9f,0x21,0xd1,0x21,0xd1,0x2e,0xb4,0x4a,0x93,0x0a,0x41,0xa1,0x6a,0xe7,0x5f,0x7f},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { STORSEC_PREFIX,   STRLEN_CHAR16(STORSEC_PREFIX),   1,
     {0xfe,0x45,0xdb,0x02,0x1b,0xad,0xb6,0x4c,0xae,0xcc,0x00,0x42,0xc6,0x37,0xde,0xfa},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { VBMETA_PREFIX,    STRLEN_CHAR16(VBMETA_PREFIX),    0, {0x0}, {0x0}, 1 },
   { VENDOR_PREFIX,    STRLEN_CHAR16(VENDOR_PREFIX),    0, {0x0}, {0x0}, 1 },
   { XBL_PREFIX,       STRLEN_CHAR16(XBL_PREFIX),       1,
     {0x2c,0xba,0xa0,0xde,0xdd,0xcb,0x05,0x48,0xb4,0xf9,0xf4,0x28,0x25,0x1c,0x3e,0x98},
     {0xa2,0xa0,0xd0,0xeb,0xe5,0xb9,0x33,0x44,0x87,0xc0,0x68,0xb6,0xb7,0x26,0x99,0xc7},
     1
   },
   { XBLCONFIG_PREFIX, STRLEN_CHAR16(XBLCONFIG_PREFIX), 1,
     {0xe4,0x5a,0x32,0x5a,0x76,0x42,0x6d,0xb6,0x0a,0xdd,0x34,0x94,0xdf,0x27,0x70,0x6a},
     {0xa2,0xa0,0xd0,0xeb,0xe5,0xb9,0x33,0x44,0x87,0xc0,0x68,0xb6,0xb7,0x26,0x99,0xc7},
     1
   },
   { MDTP_PREFIX,        STRLEN_CHAR16(MDTP_PREFIX),    1,
     {0x8a,0x40,0x78,0x38,0x63,0xe2,0x67,0x4b,0xb8,0x78,0x63,0x40,0xb3,0x5b,0x11,0xe3},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { MDTPSECAPP_PREFIX,  STRLEN_CHAR16(MDTPSECAPP_PREFIX),  1,
     {0x80,0xd6,0x02,0xea,0x12,0x87,0x52,0x45,0xa3,0xbe,0xe6,0x08,0x78,0x29,0xc1,0xe6},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { IMAGEFV_PREFIX,     STRLEN_CHAR16(IMAGEFV_PREFIX),     1,
     {0x77,0x11,0x91,0x17,0xe6,0xc9,0x72,0x43,0x93,0x3c,0x80,0x4b,0x67,0x8e,0x66,0x6f},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { DTBO_PREFIX, STRLEN_CHAR16(DTBO_PREFIX), 1,
     {0x18,0xd4,0xd0,0x24,0x1d,0xd3,0x8d,0x4d,0xac,0x2c,0x4d,0x43,0x05,0x18,0x84,0x50},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { UEFI_PREFIX, STRLEN_CHAR16(UEFI_PREFIX), 1,
     {0xcd,0xfd,0x0f,0x40,0xe0,0x22,0xe7,0x47,0x9a,0x23,0xf1,0x6e,0xd9,0x38,0x23,0x88},
     {0x5b,0x4b,0x23,0x9f,0xfb,0x0e,0x13,0x43,0x8e,0x4c,0x0a,0xf1,0xf6,0x05,0x53,0x6b},
     1
   },
   { UEFISECAPP_PREFIX, STRLEN_CHAR16(UEFISECAPP_PREFIX), 1,
     {0x08,0x7e,0x8a,0xbe,0x7a,0x1b,0xae,0x4c,0x99,0x3a,0xd5,0xb7,0xfb,0x55,0xb3,0xc2},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { CORENHLOS_PREFIX, STRLEN_CHAR16(CORENHLOS_PREFIX), 1,
     {0xce,0xb4,0x90,0x66,0xe9,0x70,0x17,0x48,0xb9,0xf1,0x25,0xd6,0x4d,0x88,0x83,0x57},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { MULTIIMGOEM_PREFIX, STRLEN_CHAR16(MULTIIMGOEM_PREFIX), 1,
     {0x36,0xa4,0x26,0xe1,0x7e,0x75,0xd0,0x42,0x8d,0x19,0x0f,0x36,0x2f,0x7a,0x62,0xb8},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { MULTIIMGQTI_PREFIX, STRLEN_CHAR16(MULTIIMGQTI_PREFIX), 1,
     {0x05,0x6f,0x6c,0x84,0x46,0xeb,0x0a,0x4c,0xa1,0xa3,0x36,0x48,0xef,0x3f,0x9d,0x0e},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { VMLINUX_PREFIX, STRLEN_CHAR16(VMLINUX_PREFIX), 1,
     {0x8c,0xe7,0x67,0xcb,0x27,0xd4,0x1b,0x45,0x8f,0xef,0x94,0x49,0xfb,0x3c,0xcf,0x27},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { FEATENABLER_PREFIX, STRLEN_CHAR16(FEATENABLER_PREFIX), 1,
     {0xd2,0x13,0x18,0x74,0x87,0x8c,0x65,0x44,0x8c,0x69,0x03,0x2c,0x77,0x1c,0xcc,0xe7},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { SHRM_PREFIX,       STRLEN_CHAR16(SHRM_PREFIX),  1,
     {0x22,0xca,0x74,0xcb,0x0d,0x2f,0x82,0x4b,0xa1,0xd6,0xc4,0x21,0x3f,0x34,0x8d,0x73},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { VENDOR_BOOT_PREFIX,       STRLEN_CHAR16(VENDOR_BOOT_PREFIX),  1,
     {0x7f,0x6a,0x28,0x6d,0x1e,0x9f,0xb3,0x46,0xac,0xf4,0x96,0xa0,0x59,0x1f,0xe6,0x6f},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { VENDOR_DLKM_PREFIX,       STRLEN_CHAR16(VENDOR_DLKM_PREFIX),  0,
     {0x83,0x38,0xb3,0xaf,0x71,0xfc,0x0a,0x41,0xa4,0xaa,0x71,0xec,0x21,0x5c,0x55,0x0a},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
   { CPUCP_PREFIX,       STRLEN_CHAR16(CPUCP_PREFIX),  1,
     {0xbd,0x15,0x86,0x1e,0x8c,0x6d,0xad,0x41,0xb3,0xea,0x50,0xe8,0xbf,0x40,0xe4,0x3f},
     {0xd4,0x6c,0x03,0x77,0xd5,0x03,0xbb,0x42,0x8e,0xd1,0x37,0xe5,0xa8,0x8b,0xaa,0x34},
     1
   },
};

static int g_total_slots = -1;

static int strcmp16(const char16_t *s1, const char16_t *s2)
{
   if(NULL == s1 && NULL != s2)
      return -1;
   if(NULL != s1 && NULL == s2)
      return 1;
   if(NULL == s1 && NULL == s2)
      return 0;

   while((*s1 == *s2) && *s1 && *s2) {
      s1++;
      s2++;
   }

   return (*s1-*s2);
}

/*
 * Get boot device via string "androidboot.bootdevice=xxx.xxx"
 * in /proc/cmdline
 *
 * return value:
 *    -1: error, 1: ufs, 0: emmc
 */
static int get_boot_dev_type()
{
   int fd = -1;
   ssize_t read_bytes = 0;
   char *token[CMDLINE_LEN] = {NULL};
   char *outer_ptr = NULL;
   uint16_t index_str = 0;
   char *cmdline = NULL;
   static int boot_type = TYPE_ERROR;

   if(TYPE_UFS == boot_type ||
      TYPE_EMMC == boot_type) {
      /* already get type before */
      return boot_type;
   }

   do {
      fd = open(KERNEL_CMDLINE, O_RDONLY, 0);
      if (fd == -1) {
         fprintf(stderr, "%s: open: %s\n",
               __func__, strerror(errno) );
         break;
      }

      cmdline = (char *)malloc(CMDLINE_LEN);
      if (!cmdline) {
         fprintf(stderr, "%s: malloc: %s\n",
               __func__, strerror(errno) );
         break;
      }

      read_bytes = read(fd, cmdline, CMDLINE_LEN -1);
      if (-1 == read_bytes) {
         fprintf(stderr, "%s: read: %s\n", __func__, strerror(errno));
         break;
      }

      token[index_str] = strtok_r(cmdline, " ",&outer_ptr);
      while (  token[index_str] != NULL) {
         if (!strncmp(token[index_str], CMDLINE_BOOT_DEV,
                     strlen(CMDLINE_BOOT_DEV))) {
            break;
         }
          index_str++;
         token[index_str] = strtok_r(NULL, " ",&outer_ptr);
      }

      if(!token[index_str]) {
         fprintf(stderr, "%s: %s not in kernel cmdline\n",
                     __func__, CMDLINE_BOOT_DEV);
         break;
      } else if(strstr(token[index_str],".ufshc")) {
         boot_type = TYPE_UFS;
      } else {
         boot_type = TYPE_EMMC;
      }
   } while(0);

//clean on exit:
   if (fd >= 0)
      close(fd);
   if (cmdline)
      free(cmdline);

   return boot_type;
}

/* Return dev_node of disk(LU) which holds the specific partition
 * TODO:
 *     get dev node in run-time
 */
static const char* get_disk_from_part(const char16_t *partition_name)
{
   const char* dev = NULL;
   int boot_type = get_boot_dev_type();

   if(TYPE_ERROR == boot_type)
      return NULL;

   //emmc: all paritions are held by disk "/dev/mmcblk0"
   if(TYPE_EMMC == boot_type) {
      #ifdef __USE_SIMULATION__
         return "/dev/loop0";
      #else
         return "/dev/mmcblk0";
      #endif
   }

   //ufs: different paritions may be held by different disk(LU)
   if (!(strcmp16(partition_name, (u"system_a"))) ||
         !(strcmp16(partition_name, (u"system_b"))))
      dev = "/dev/sda";
   else if (!(strcmp16(partition_name, (u"xbl_a"))) ||
            !(strcmp16(partition_name, (u"xbl_config_a"))))
      dev = "/dev/sdb";
   else if (!(strcmp16(partition_name, (u"xbl_b"))) ||
            !(strcmp16(partition_name, (u"xbl_config_b"))))
      dev = "/dev/sdc";
   else
      dev = "/dev/sde";

   return dev;
}

/* Get scsi_node(/dev/sgX) according to
 * boot-LUN node(/dev/block/bootdevice/by-name/xbl_X)
 */
static int get_scsi_node_from_bootdevice(const char *bootdev_path,
                char *sg_node_path,
                size_t buf_size)
{
   char sg_dir_path[PATH_MAX] = {0};
   char real_path[PATH_MAX] = {0};
   DIR *scsi_dir = NULL;
   struct dirent *de;
   int node_found = 0;
   if (!bootdev_path || !sg_node_path) {
          fprintf(stderr, "%s : invalid argument\n", __func__);
          goto error;
   }
   if (realpath(bootdev_path, real_path) == NULL) {
      fprintf(stderr, "failed to resolve link for %s: %s\n",
                                  bootdev_path,
                                  strerror(errno));
                  goto error;
   }
   if(strlen(real_path) < PATH_TRUNCATE_LOC + 1){
      fprintf(stderr, "Unrecognized path :%s:\n",
                     real_path);
      goto error;
   }
   //For the safe side in case there are additional partitions on
   //the XBL lun we truncate the name.
   real_path[PATH_TRUNCATE_LOC] = '\0';
   if(strlen(real_path) < LUN_NAME_START_LOC + 1){
      fprintf(stderr, "Unrecognized truncated path :%s:\n",
                     real_path);
      goto error;
   }
   //This will give us /dev/block/sdb/device/scsi_device
   //which contains a file 0:0:0:Y whose name gives us the path
   //to /dev/bsg/0:0:0:Y
   snprintf(sg_dir_path, sizeof(sg_dir_path) - 1,
                  "/sys/block/%s/device/scsi_device", &real_path[LUN_NAME_START_LOC]);

   scsi_dir = opendir(sg_dir_path);
   if (!scsi_dir) {
          fprintf(stderr, "%s : Failed to open %s(%s)\n",
                          __func__,
                          sg_dir_path,
                          strerror(errno));
          goto error;
   }
   while((de = readdir(scsi_dir))) {
          if (de->d_name[0] == '.')
                  continue;
          else if (!strncmp(de->d_name, "0:", 2)) {
                    snprintf(sg_node_path,
                                  buf_size -1,
                                  "/dev/bsg/%s",
                                  de->d_name);
                    node_found = 1;
                    break;
          }
   }
   if(!node_found) {
          fprintf(stderr,"%s: Unable to locate scsi device node\n",
                         __func__);
          goto error;
   }
   closedir(scsi_dir);
   return 0;

error:
   if (scsi_dir)
          closedir(scsi_dir);
   return -1;
}

static int set_xbl_boot_partition(unsigned int slot)
{
   char sg_dev_node[PATH_MAX] = {0};
   uint8_t boot_lun_id = 0;
   const char *boot_dev = NULL;

   if (slot == 0) {
      boot_lun_id = BOOT_LUN_A_ID;
      boot_dev = XBL_AB_PRIMARY;
   } else if (slot == 1) {
      boot_lun_id = BOOT_LUN_B_ID;
      boot_dev = XBL_AB_SECONDARY;
   } else {
      fprintf(stderr, "%s: Invalid slot id(%d)\n", __func__, slot);
      goto error;
   }

   fprintf(stderr, "%s: setting %s lun as boot lun\n",
                           __func__, boot_dev);

   if (get_scsi_node_from_bootdevice(boot_dev,
                        sg_dev_node, PATH_MAX)) {
      fprintf(stderr, "%s: failed to get scsi node path for %s\n",
                           __func__, boot_dev);
      goto error;
   }

   if (set_boot_lun(sg_dev_node, boot_lun_id)) {
      fprintf(stderr, "%s: failed to set boot lun\n", __func__);
      goto error;
   } else {
      fprintf(stderr, "%s: set new boot lun done\n", __func__);
   }

   return 0;

error:
   return -1;
}

static const char16_t* get_partition_name(unsigned int slot)
{
   switch (slot) {
      case 0:
         return u"boot_a";
         break;

      case 1:
         return u"boot_b";
         break;

      case 2:
         return u"boot_c";
         break;

      default:
         return NULL;
   }
   return NULL;
}

static int get_total_slots () {
   int total_slots = 0;
   const char16_t* boot_partition[] = {u"boot_a", u"boot_b", u"boot_c", NULL};
   gpt_partition_entry_t e;
   const char *disk_node = get_disk_from_part(boot_partition[0]);

   if (g_total_slots != -1)
      return g_total_slots;

   for (int i = 0; boot_partition[i] != 0; i++) {
      int ret = libgpt_getPartitionEntry(disk_node, boot_partition[i], &e);
      if(ret == -1) {
         fprintf(stderr, "%s: failed to get part-entry for %s\n",
                  __func__, disk_node);
         break;
      }
      total_slots++;
   }

   g_total_slots = total_slots;

   fprintf(stderr, "%s: total slots %d\n", __func__, total_slots);

   return total_slots;
}

int libabctl_getPartitionIndexForSlot(int slot, char* device)
{
   int ret = -1;
   const char *disk_node = NULL;

   assert(slot < get_total_slots());

   const char16_t* boot_partition = NULL;

   switch (slot)
   {
   case 0:
      boot_partition = u"boot_a";
      break;

   case 1:
      boot_partition = u"boot_b";
      break;

   case 2:
      boot_partition = u"boot_c";
      break;

   default:
      return -1;
   }

   disk_node = get_disk_from_part(boot_partition);
   if(NULL == disk_node)
      return -1;

   ret = libgpt_getPartitionEntry(disk_node, boot_partition, NULL);

   return ret;
}

int libabctl_getBootSlot()
{
   int boot_slot = -1;
   char slot_suffix[4] = {0};
   char *cmdline = NULL;
   int slot_suffix_found = 0;
   int fd = -1;
   ssize_t read_bytes = 0;
   char *key_val_pair = NULL;
   char *save_ptr = NULL;

   if (getenv("SLOT_SUFFIX") != NULL) {
        strlcpy(slot_suffix, getenv("SLOT_SUFFIX"), sizeof(slot_suffix));
   } else {
      do {
         fd = open(KERNEL_CMDLINE, O_RDONLY, 0);
         if ( fd == -1) {
            perror("libabctl: open: KERNEL_CMDLINE ");
            break;
         }

         cmdline = (char *)malloc(CMDLINE_LEN + 1);
         if (!cmdline) {
            perror("libabctl: malloc: ");
            break;
         }

         read_bytes = read(fd, cmdline, CMDLINE_LEN);
         if ( read_bytes == -1) {
            perror("libabctl: read: KERNEL_CMDLINE ");
            free(cmdline);
            close(fd);
            break;
         }
         close(fd);

         key_val_pair = strtok_r(cmdline, " ", &save_ptr);

         while (key_val_pair != NULL) {
            slot_suffix_found =
               (0 == strncmp(key_val_pair,
                             SLOT_SUFFIX_STR,
                             SLOT_SUFFIX_STRLEN))?1:0;

            if (slot_suffix_found) {
               strlcpy(slot_suffix,
                      &key_val_pair[SLOT_SUFFIX_STRLEN - 1],
                      sizeof(slot_suffix));
            }
            key_val_pair = strtok_r(NULL, " ", &save_ptr);
         }
      } while (0);
   }

   if (slot_suffix[0] != 0) {
        switch(slot_suffix[1]) {
            case 'a' : {
               boot_slot = 0;
               break;
            }
            case 'b' : {
               boot_slot = 1;
               break;
            }
            case 'c' : {
               boot_slot = 2;
               break;
            }
            default  : {
               fprintf(stderr, "Unknown Slot\n");
            }
        }
   }

   if (cmdline)
      free(cmdline);

   return boot_slot;
}

int libabctl_SetBootSuccess()
{
   int ret = libabctl_getBootSlot();
   gpt_partition_entry_t e;
   const char16_t *boot_partition = NULL;
   const char *disk_node = NULL;

   if(ret == -1) {
      fprintf(stderr, "%s: fail to get boot slot\n", __func__);
      goto out;
   }

   boot_partition = get_partition_name(ret);
   disk_node = get_disk_from_part(boot_partition);

   if(NULL == disk_node)
      goto out;

   ret = libgpt_getPartitionEntry(disk_node, boot_partition, &e);
   if(ret == -1) {
      fprintf(stderr, "%s: fail to get part-entry for %s\n",
                     __func__, disk_node);
      goto out;
   }

   e.attribute_flags |= PARTITION_ATTRIBUTE_SUCCESSFUL_VAL;
   e.attribute_flags &= PARTITION_ATTRIBUTE_UNBOOTABLE_CLR;

   ret = libgpt_setAttribute(disk_node, boot_partition, e.attribute_flags);

out:
   return ret;
}

int libabctl_getActiveStatus(unsigned int slot)
{
   int ret = -1;
   gpt_partition_entry_t e;
   const char16_t* boot_partition = NULL;

   switch (slot) {
   case 0:
      boot_partition = u"boot_a";
      break;

   case 1:
      boot_partition = u"boot_b";
      break;

   case 2:
      boot_partition = u"boot_c";
      break;

   default:
      return -1;
   }

   const char *disk_node = get_disk_from_part(boot_partition);

   if(NULL == disk_node)
      goto out;

   ret = libgpt_getPartitionEntry(disk_node, boot_partition, &e);
   if(ret == -1) {
      perror("libabctl_getActiveStatus: ");
      goto out;
   }

   ret = (e.attribute_flags & PARTITION_ATTRIBUTE_ACTIVE_VAL)? 1 : 0;

out:
   return ret;
}

int libabctl_setActive(unsigned int slot)
{
   int ret = -1;
   gpt_partition_entry_t e;
   char16_t active_slot_suffix;
   char16_t non_active_slot_suffix[3] = {0, 0, 0};
   int iter = 0;
   const char *disk_node = NULL;
   struct stat st;
   unsigned int xbl_in_use = 0;
   int total_slots = get_total_slots();
   assert(slot < get_total_slots());

   char16_t active_partition[GPT_NAME_MAX] = {0};
   char16_t non_active_partition[GPT_NAME_MAX] = {0};
   int boot_type = get_boot_dev_type();

   if(TYPE_ERROR == boot_type)
      goto out;

   /*Determine suffix letter from the slot ID*/
   switch (slot) {
      case 0: {
         active_slot_suffix = 'a';
         non_active_slot_suffix[0] = 'b';
         if (total_slots == 3)
            non_active_slot_suffix[1] = 'c';
      }
      break;

      case 1: {
         active_slot_suffix = 'b';
         non_active_slot_suffix[0] = 'a';
         if (total_slots == 3)
            non_active_slot_suffix[1] = 'c';
      }
      break;

      case 2: {
         active_slot_suffix = 'c';
         non_active_slot_suffix[0] = 'a';
         if (total_slots == 3)
            non_active_slot_suffix[1] = 'b';
      }
      break;

      default:
         goto out;
   }

   /*Iterate through all the partitons that are impacted by AB*/
   for( iter = 0; iter < sizeof(part_guid_map)/sizeof(partition_guid_map_t);
        iter++) {
      /* skip xbl_x & xbl_config_x which are inside UFS boot partition. */
      if (boot_type == TYPE_UFS &&
        ! (strcmp16(part_guid_map[iter].partition_name_prefix,
                          u"xbl_"))) {
          xbl_in_use = 1;
          continue;
      }

      /*Copy the partition name prefix.*/
      memset(active_partition, 0, GPT_NAME_MAX);
      memcpy(active_partition, part_guid_map[iter].partition_name_prefix,
                               part_guid_map[iter].prefix_len * sizeof(char16_t));
      /*concat the active-suffix to get complete partition name*/
      active_partition[part_guid_map[iter].prefix_len] = active_slot_suffix;

      disk_node = get_disk_from_part(active_partition);

      ret = libgpt_getPartitionEntry(disk_node, active_partition, &e);
      if(ret == -1) {
         if (part_guid_map[iter].ignore_if_missing) {
            /* If partition is okay to be ignored, reset the return code.*/
            ret = 0;
            continue;
         }
         fprintf(stderr,"%s: failed to get active part-entry for %s\n",
                        __func__, disk_node);
         goto out;
      }

      e.attribute_flags |= (PARTITION_ATTRIBUTE_ACTIVE_VAL |
                            PARTITION_ATTRIBUTE_PRIORITY_VAL_MAX |
                            PARTITION_ATTRIBUTE_MAX_RETRY_COUNT_VAL);
      e.attribute_flags &= PARTITION_ATTRIBUTE_SUCCESSFUL_CLR;
      e.attribute_flags &= PARTITION_ATTRIBUTE_UNBOOTABLE_CLR;
      ret = libgpt_setAttribute(disk_node, active_partition,
                                                e.attribute_flags);

      if(part_guid_map[iter].uses_part_type_guid) {
         ret = libgpt_setTypeGUID(disk_node, active_partition,
                                  part_guid_map[iter].active_slot_guid);
      }

      if(ret == -1) {
         fprintf(stderr,"%s: failed to update GPT", __func__);
         goto out;
      }

      int boot_slot = libabctl_getBootSlot();

      for (int idx = 0; non_active_slot_suffix[idx]; idx++) {
         /*Update for non-active partitions*/
         /*Copy the partition name prefix.*/
         memset(non_active_partition, 0, sizeof(non_active_partition));
         memcpy(non_active_partition, part_guid_map[iter].partition_name_prefix,
                                      part_guid_map[iter].prefix_len * sizeof(char16_t));
         /*concat the non-active-suffix to get complete partition name*/
         non_active_partition[part_guid_map[iter].prefix_len] = non_active_slot_suffix[idx];

         disk_node = get_disk_from_part(non_active_partition);

         ret = libgpt_getPartitionEntry(disk_node, non_active_partition, &e);
         if(ret == -1) {
            fprintf(stderr,"%s: failed to get non-active part-entry for %s\n",
                    __func__, disk_node);
            goto out;
         }

         e.attribute_flags &= PARTITION_ATTRIBUTE_PRIORITY_CLR;
         if (boot_slot != -1 && (('a' + boot_slot) == non_active_slot_suffix[idx]))
            e.attribute_flags
               |= ((uint64_t)(0x2) << PARTITION_ATTRIBUTE_PRIORITY_BIT_POS);
         else
            e.attribute_flags |= PARTITION_ATTRIBUTE_PRIORITY_VAL_MIN;
         ret = libgpt_setAttribute(disk_node, non_active_partition, e.attribute_flags);

         if(part_guid_map[iter].uses_part_type_guid) {
            ret = libgpt_setTypeGUID(disk_node, non_active_partition,
                                     part_guid_map[iter].dormant_slot_guid);
         }
         if(ret == -1) {
            fprintf(stderr,"%s: failed to update non-active GPT\n", __func__);
            goto out;
         }
      }
   }

   if(TYPE_UFS == boot_type) {
      if(xbl_in_use)
      {  //make sure dev nodes exists before switching xbl slots.
         while (stat(XBL_AB_PRIMARY, &st) || stat(XBL_AB_SECONDARY, &st));
         ret = set_xbl_boot_partition(slot);
         if(ret)
             fprintf(stderr, "%s: failed to set boot lun\n", __func__);
       }
      else
         fprintf(stderr, "%s: xbl part not found(%s). Assuming sbl in use\n", __func__, strerror(errno));
   }

out:
   return ret;
}

int libabctl_setPriority(unsigned int slot, unsigned int priority){
    int ret = -1;
    gpt_partition_entry_t e;
    char16_t slot_suffix ;
    int iter = 0;
    const char *disk_node = NULL;
    unsigned int xbl_in_use = 0;

    assert(slot < get_total_slots());

    char16_t active_partition[GPT_NAME_MAX] = {0};
    int boot_type = get_boot_dev_type();

    if(TYPE_ERROR == boot_type)
      goto out;
    /*Check priority in range 0 - 3*/
    if(priority > uint64_t(0x3))
        return -1;
    else{
        /*Determine suffix letter from the slot ID*/
        switch (slot) {
            case 0: {
                slot_suffix = 'a';
            }
            break;
            case 1: {
                slot_suffix = 'b';
            }
            break;
            case 2: {
                slot_suffix = 'c';
            }
            break;
            default:
                goto out;
        }

        /*Iterate through all the partitions of that slotID type, e.g., ${name}_c */
        for( iter = 0; iter < sizeof(part_guid_map)/sizeof(partition_guid_map_t);
             iter++) {
              /* skip xbl_x & xbl_config_x which are inside UFS boot partition. */
              if (boot_type == TYPE_UFS &&
                  ! (strcmp16(part_guid_map[iter].partition_name_prefix,
                              u"xbl_")))
               {
                  xbl_in_use = 1;
                  continue;
               }
              /*Copy the partition name prefix.*/
              memset(active_partition, 0, GPT_NAME_MAX);
              memcpy(active_partition, part_guid_map[iter].partition_name_prefix,
                                       part_guid_map[iter].prefix_len * sizeof(char16_t));
              /*concat the active-suffix to get complete partition name*/
              active_partition[part_guid_map[iter].prefix_len] = slot_suffix;

              disk_node = get_disk_from_part(active_partition);

              ret = libgpt_getPartitionEntry(disk_node, active_partition, &e);
              if(ret == -1) {
                 if (part_guid_map[iter].ignore_if_missing) {
                    /* If partition is okay to be ignored, reset the return code.*/
                    ret = 0;
                    continue;
                 }
                 fprintf(stderr,"%s: failed to get active part-entry for %s\n while setting priority",
                                __func__, disk_node);
                 goto out;
              }

              e.attribute_flags &= PARTITION_ATTRIBUTE_PRIORITY_CLR;
              e.attribute_flags |= ((uint64_t)(priority) << PARTITION_ATTRIBUTE_PRIORITY_BIT_POS);
              ret = libgpt_setAttribute(disk_node, active_partition,
                                                        e.attribute_flags);
             }
        }
out:
   return ret;
}

int libabctl_getSuccessStatus(unsigned int slot)
{
   int ret = -1;
   gpt_partition_entry_t e;
   const char16_t* boot_partition = NULL;

   switch (slot) {
   case 0:
      boot_partition = u"boot_a";
      break;

   case 1:
      boot_partition = u"boot_b";
      break;

   case 2:
      boot_partition = u"boot_c";
      break;

   default:
      return -1;
   }
   const char *disk_node = get_disk_from_part(boot_partition);

   if(NULL == disk_node)
      goto out;

   ret = libgpt_getPartitionEntry(disk_node, boot_partition, &e);
   if(ret == -1) {
      perror("libabctl_getSuccessStatus: ");
      goto out;
   }

   ret = (e.attribute_flags & PARTITION_ATTRIBUTE_SUCCESSFUL_VAL) ? 1 : 0;

out:
   return ret;
}

int libabctl_setUnbootable(unsigned int slot)
{
   const char16_t *partition = get_partition_name(slot);
   int ret = -1;
   gpt_partition_entry_t e;
   const char *disk_node = NULL;

   if(partition == NULL)
      goto out;

   disk_node = get_disk_from_part(partition);
   if(NULL == disk_node)
      goto out;

   ret = libgpt_getPartitionEntry(disk_node, partition, &e);
   if(ret == -1) {
      perror("libabctl_setUnbootable: ");
      goto out;
   }

   e.attribute_flags |= PARTITION_ATTRIBUTE_UNBOOTABLE_VAL;
   e.attribute_flags &= ~(PARTITION_ATTRIBUTE_SUCCESSFUL_VAL |
                          PARTITION_ATTRIBUTE_MAX_RETRY_COUNT_VAL |
                          PARTITION_ATTRIBUTE_ACTIVE_VAL |
                          PARTITION_ATTRIBUTE_PRIORITY_VAL_MAX);

   ret = libgpt_setAttribute(disk_node, partition, e.attribute_flags);

out:
   return ret;
}

void libabctl_debugPrints(int do_prints)
{
   libgpt_dbg_prints(do_prints);
}
