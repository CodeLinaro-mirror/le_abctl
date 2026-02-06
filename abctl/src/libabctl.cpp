// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
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
#include <abctl-ufs-bsg.h>
#include <iostream>
#include <vector>
#ifdef LVMISC_PARTITION
#include "abctl-lvmisc.h"
#endif
#include <abctl-mmc.h>

#define XBL_AB_PRIMARY      "/dev/disk/by-partlabel/xbl_a"
#define XBL_AB_SECONDARY    "/dev/disk/by-partlabel/xbl_b"
#define CMDLINE_BOOT_DEV    "androidboot.bootdevice="

//boot device type
#define TYPE_ERROR   -1
#define TYPE_EMMC    0
#define TYPE_UFS     1

#define BOOT_LUN_A_ID 1
#define BOOT_LUN_B_ID 2

#define SLOT_SUFFIX_STRLEN   (sizeof("androidboot.slot_suffix=_") - sizeof(char))
#define KERNEL_CMDLINE       ("/proc/cmdline")
#define CMDLINE_LEN          (4096)

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

#define MAX_SLOTS                                 (2)

#define LIST_SIZE(x) (sizeof(x) / sizeof((x)[0]))

//consistent with the partition list in android PTN_SWAP_LIST
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
#define CMNLIB32_PREFIX   (u"cmnlib32_")
#define CMNLIB64_PREFIX   (u"cmnlib64_")
#define KEYMASTER_PREFIX  (u"keymaster_")
#define ABL_PREFIX        (u"abl_")
#define AOP_PREFIX        (u"aop_")
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
#define IMAGEFV_PREFIX    (u"imagefv_")
#define PMIC_PREFIX       (u"pmic_")
#define APDP_PREFIX       (u"apdp_")
#define HOSD_PREFIX       (u"hosd_")
#define KEYSTORE_PREFIX   (u"keystore_")
#define MSADP_PREFIX      (u"msadp_")
#define DTBO_PREFIX       (u"dtbo_")

#define PARTITION_PREFIX_LIST SBL_PREFIX, RPM_PREFIX, TZ_PREFIX, ABOOT_PREFIX, BOOT_PREFIX, MODEM_PREFIX, DSP_PREFIX, SYSTEM_PREFIX, DEVCFG_PREFIX, LKSECAPP_PREFIX, CMNLIB_PREFIX, CMNLIB32_PREFIX, CMNLIB64_PREFIX, KEYMASTER_PREFIX, ABL_PREFIX, AOP_PREFIX, BLUETOOTH_PREFIX, HYP_PREFIX, QUPFW_PREFIX, STORSEC_PREFIX, VBMETA_PREFIX, VENDOR_PREFIX, XBL_PREFIX, XBLCONFIG_PREFIX, MDTP_PREFIX, MDTPSECAPP_PREFIX, IMAGEFV_PREFIX, PMIC_PREFIX, APDP_PREFIX, HOSD_PREFIX, KEYSTORE_PREFIX, MSADP_PREFIX, DTBO_PREFIX

const char16_t* part_list[] = {PARTITION_PREFIX_LIST};

typedef struct {
   char16_t partition[GPT_NAME_MAX];
   char disk[PATH_MAX];
   guid_t partition_typeguid;
} partition_info;

/* partition and disk info arrays */
std::vector <partition_info> active_info, non_active_info;

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

static size_t strlen16(const char16_t* str16) {
   size_t count = 0;
   while (*str16 != u'\0') {
      str16++;
      count++;
   }
   return count;
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
   char *token = NULL;
   static int boot_type = TYPE_ERROR;
   char *saveptr = NULL;
   char cmdline[CMDLINE_LEN + 1] = {0};

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

      read_bytes = read(fd, cmdline, CMDLINE_LEN);
      if (-1 == read_bytes) {
         fprintf(stderr, "%s: read: %s\n", __func__, strerror(errno));
         break;
      }

      // Ensure cmdline is null-terminated
      if (read_bytes >= CMDLINE_LEN) {
         read_bytes = CMDLINE_LEN - 1;
      }
      cmdline[read_bytes] = '\0';

      token = strtok_r(cmdline, " ", &saveptr);
      while (token != NULL) {
         if (!strncmp(token, CMDLINE_BOOT_DEV,
                     strlen(CMDLINE_BOOT_DEV))) {
            break;
         }
         token = strtok_r(NULL, " ", &saveptr);
      }

      if(!token) {
         fprintf(stderr, "%s: %s not in kernel cmdline\n",
                     __func__, CMDLINE_BOOT_DEV);
         break;
      } else if(strstr(token,".ufshc")) {
         boot_type = TYPE_UFS;
      } else {
         boot_type = TYPE_EMMC;
      }
   } while(0);

//clean on exit:
   if (fd >= 0)
      close(fd);

   return boot_type;
}

/*
 * Return dev_node of disk(LU) which holds the specific partition
 */
static const char* get_disk_from_part(const char16_t *partition_name)
{
   int boot_type = get_boot_dev_type();

   if(TYPE_ERROR == boot_type)
      return NULL;

   //emmc: all paritions are held by disk "/dev/mmcblk0"
   if(TYPE_EMMC == boot_type)
      return "/dev/mmcblk0";

   //ufs: different paritions may be held by different disk(LU)
   char partition[PATH_MAX] = {0};
   char partition_path[PATH_MAX] = {0};
   char realtive_path[PATH_MAX] = {0};
   char sys_path[PATH_MAX] = {0};
   char realtive_sys_path[PATH_MAX] = {0};
   static char disk_path[PATH_MAX] = {0};
   char *device_name = nullptr;
   char *disk_name = nullptr;
   mbstate_t state{};

   // Convert const char16_t* to char[]
   size_t sz = std::char_traits<char16_t>::length(partition_name);
   char *p = partition;
   for(size_t n = 0; n < sz; ++n) {
       int rc = c16rtomb(p, partition_name[n], &state);
       if(rc == -1) break;
       p += rc;
   }

   // Get partition path. (e.g. /dev/disk/by-partlabel/xbl_a)
   snprintf(partition_path, sizeof(partition_path), "/dev/disk/by-partlabel/%s", partition);
   // Get realtive path. (e.g. ../../sdb1)
   if (readlink(partition_path, realtive_path, PATH_MAX - 1) < 0) {
      fprintf(stderr, "%s : Failed to resolve link for %s: %s\n", __func__, partition_path, strerror(errno));
      return NULL;
   }
   // Get device name. (e.g. sdb1)
   device_name = strrchr(realtive_path, '/');
   if (!device_name) {
      fprintf(stderr, "%s : Get device name for partition %s error\n", __func__, partition_path);
      return NULL;
   }
   device_name++;

   // Get sys path (e.g. /sys/class/block/sdb1)
   snprintf(sys_path, sizeof(sys_path), "/sys/class/block/%s", device_name);
   // Get realtive sys path (e.g. ../../devices/platform/soc/1d84000.ufshc/host0/target0:0:0/0:0:0:1/block/sdb/sdb1)
   if (readlink(sys_path, realtive_sys_path, PATH_MAX - 1) < 0) {
      fprintf(stderr, "%s: Failed to resolve link for %s: %s\n", __func__, sys_path, strerror(errno));
      return NULL;
   }
   // Get realtive sys parent path(e.g. ../../devices/platform/soc/1d84000.ufshc/host0/target0:0:0/0:0:0:1/block/sdb)
   disk_name = strrchr(realtive_sys_path, '/');
   if (!disk_name) {
      fprintf(stderr, "%s : Get relative sys partent path for partition %s error\n", __func__, partition_path);
      return NULL;
   }
   *disk_name = '\0';
   // Get disk name (e.g. sdb)
   disk_name = strrchr(realtive_sys_path, '/');
   if (!disk_name) {
      fprintf(stderr, "%s : Get disk name for partition %s error \n", __func__, partition_path);
      return NULL;
   }
   disk_name++;
   // Get disk path: /dev/sdb
   snprintf(disk_path, sizeof(disk_path), "/dev/%s", disk_name);
   printf("get_disk_from_part(partition:%s) return %s\n", partition, disk_path);
   return disk_path;
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
   char* device_path = nullptr;
   const size_t device_name_length = 3;  // e.g. sda, sdb

   if (!bootdev_path || !sg_node_path) {
          fprintf(stderr, "%s : invalid argument\n", __func__);
          goto error;
   }
   if (readlink(bootdev_path, real_path, PATH_MAX - 1) < 0) {
      fprintf(stderr, "failed to resolve link for %s: %s\n",
                                  bootdev_path,
                                  strerror(errno));
      goto error;
   }

   // readlink returns a relative path (e.g. ../../sdb1)
   // return the filename(real_path) (e.g. sdb1)
   device_path = strrchr(real_path, '/');
   if (NULL == device_path) {
      fprintf(stderr, "%s : Get device relative path error\n", __func__);
      goto error;
   } else {
      device_path++;
   }
   // then subtract the number suffix (e.g. sdb)
   device_path[device_name_length] = '\0';


#ifdef _BSG_FRAMEWORK_KERNEL_HEADERS
   //This will give us /dev/block/sdb/device/scsi_device
   //which contains a file 0:0:0:Y whose name gives us the path
   //to /dev/bsg/0:0:0:Y
   snprintf(sg_dir_path, PATH_MAX, "/sys/block/%s/device/scsi_device", device_path);
#else
   //This will give us /dev/block/sdb/device/scsi_generic
   //which contains a file sgY whose name gives us the path
   //to /dev/sgY
   snprintf(sg_dir_path, PATH_MAX, "/sys/block/%s/device/scsi_generic", device_path);
#endif
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
#ifdef _BSG_FRAMEWORK_KERNEL_HEADERS
          else if (!strncmp(de->d_name, "0:", 2)) {
                    snprintf(sg_node_path,
                                  buf_size -1,
                                  "/dev/bsg/%s",
                                  de->d_name);
                    node_found = 1;
                    break;
          }
#else
          else if (!strncmp(de->d_name, "sg", 2)) {
                    snprintf(sg_node_path,
                                  buf_size -1,
                                  "/dev/%s",
                                  de->d_name);
                    node_found = 1;
                    break;
          }
#endif
   }
   if(!node_found) {
          fprintf(stderr,"%s: Unable to locate scsi generic node\n",
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

static char16_t* get_partition_name(unsigned int slot)
{
   switch (slot) {
      case 0:
         return u"boot_a";
         break;

      case 1:
         return u"boot_b";
         break;

      default:
         return NULL;
   }
   return NULL;
}

//set partition attribute(unbootable/successful)
static int update_partition_attr(const char* device, const char16_t* partition_name, unsigned short is_active) {
   int ret = -1;
   gpt_partition_entry_t e;
   memset(&e, 0, sizeof(gpt_partition_entry_t));

   ret = libgpt_getPartitionEntry(device, partition_name, &e);
   if(ret == -1) {
      fprintf(stderr, "%s: fail to get part-entry for %s\n",
                  __func__, device);
      goto out;
   }

   if(is_active) {
      e.attribute_flags |= (PARTITION_ATTRIBUTE_ACTIVE_VAL |
                            PARTITION_ATTRIBUTE_PRIORITY_VAL_MAX |
                            PARTITION_ATTRIBUTE_MAX_RETRY_COUNT_VAL);
      e.attribute_flags &= PARTITION_ATTRIBUTE_SUCCESSFUL_CLR;
      e.attribute_flags &= PARTITION_ATTRIBUTE_UNBOOTABLE_CLR;
   } else {
      e.attribute_flags &= PARTITION_ATTRIBUTE_PRIORITY_CLR;
      e.attribute_flags |= PARTITION_ATTRIBUTE_PRIORITY_VAL_MIN;
   }

   ret = libgpt_setAttribute(device, partition_name, e.attribute_flags);
   if(ret == -1) {
      fprintf(stderr,"%s: failed to update GPT attribute for this partition", __func__);
      goto out;
   }

   return 0;

out:
   return ret;
}

//store the partition name, disk path, and partition type guid to arrays
static int store_info(const char* device, const char16_t* partition_name, unsigned short is_active) {
   int ret = -1;
   guid_t part_typeguid;
   memset(&part_typeguid, 0, sizeof(guid_t));
   partition_info info;
   memset(&info, 0, sizeof(partition_info));

   /*Get partition type guid*/
   ret = libgpt_getTypeGUID(device, partition_name, &part_typeguid);
   if(ret == -1) {
      fprintf(stderr,"%s: failed to get type guid", __func__);
      goto out;
   }
   /*Store info to the arrays*/
   memcpy(info.partition, partition_name, strlen16(partition_name) * sizeof(char16_t));
   memcpy(info.disk, device, strlen(device) + 1);
   info.partition_typeguid = part_typeguid;
   if(is_active) {
      active_info.push_back(info);
   } else {
      non_active_info.push_back(info);
   }

   return 0;
out:
   return ret;
}

//get partition infomation from the pre-populated list, then store the partition name and disk path to arrays
static int get_info_from_part_list(unsigned int slot) {
   int ret = -1;
   char16_t active_slot_suffix, non_active_slot_suffix;
   const char *disk_node = NULL;
   size_t prefix_len = 0;

   const char16_t* active_boot_partition = NULL;
   const char16_t* non_active_boot_partition = NULL;
   char16_t active_partition[GPT_NAME_MAX] = {0};
   char16_t non_active_partition[GPT_NAME_MAX] = {0};
   guid_t active_guid, non_active_guid;
   memset(&active_guid, 0, sizeof(guid_t));
   memset(&non_active_guid, 0, sizeof(guid_t));
   int iter = 0;

   /*Determine suffix letter from the slot ID*/
   switch (slot) {
      case 0:
         active_slot_suffix = 'a';
         non_active_slot_suffix = 'b';
         break;
      case 1:
         active_slot_suffix = 'b';
         non_active_slot_suffix = 'a';
         break;
      default:
         goto out;
   }

#ifdef SUPPORT_ENABLE_LV_ATOMIC_AB
   //Get active boot partition and disk
   printf("%s: SUPPORT LV ATOMIC AB.\n", __func__);
   active_boot_partition = (slot == 0 ? u"boot_a" : u"boot_b");
   disk_node = get_disk_from_part(active_boot_partition);
   if(!disk_node) {
      fprintf(stderr,"%s: failed to get disk node for active boot partition.\n", __func__);
      goto out;
   }
   /*Store active info*/
   ret = store_info(disk_node, active_boot_partition, 1);
   if(ret == -1) {
      fprintf(stderr,"%s: failed to store active boot info.\n", __func__);
      goto out;
   }

   //Get non-active boot partition and disk
   non_active_boot_partition = (slot == 0 ? u"boot_b" : u"boot_a");
   disk_node = get_disk_from_part(non_active_boot_partition);
   if(!disk_node) {
      fprintf(stderr,"%s: boot_a and boot_b are unmatched.\n", __func__);
      goto out;
   }
   /*Store non-active info*/
   ret = store_info(disk_node, non_active_boot_partition, 0);
   if(ret == -1) {
      fprintf(stderr,"%s: failed to store non-active boot info.\n", __func__);
      goto out;
   }

#else
   for( iter = 0; iter < LIST_SIZE(part_list); iter++) {
      prefix_len = strlen16(part_list[iter]);

      //Get active partition and disk
      memset(active_partition, 0, GPT_NAME_MAX);
      memcpy(active_partition, part_list[iter], prefix_len * sizeof(char16_t));
      active_partition[prefix_len] = active_slot_suffix;

      disk_node = get_disk_from_part(active_partition);
      /*If get_disk_from_part returns NULL, which means the partition doesn't exist,
        we skip adding such partition to the arrays.*/
      if(!disk_node) {
         printf("%s: the partition is missing but ok.\n", __func__);
         continue;
      }
      /*Store active info*/
      ret = store_info(disk_node, active_partition, 1);
      if(ret == -1) {
         fprintf(stderr,"%s: failed to store active partition info.\n", __func__);
         goto out;
      }

      //Get non-active partition and disk
      memset(non_active_partition, 0, GPT_NAME_MAX);
      memcpy(non_active_partition, part_list[iter], prefix_len * sizeof(char16_t));
      non_active_partition[prefix_len] = non_active_slot_suffix;
      disk_node = get_disk_from_part(non_active_partition);
      /*If get_disk_from_part returns NULL only for non-active partition,
      the A/B partitions are unmatched. */
      if(!disk_node) {
         printf("%s: the A/B partitions are unmatched.\n", __func__);
         goto out;
      }
      /*Store non-active info*/
      ret = store_info(disk_node, non_active_partition, 0);
      if(ret == -1) {
         fprintf(stderr,"%s: failed to store non-active partition info.\n", __func__);
         goto out;
      }
   }
#endif

   if(active_info.size() != non_active_info.size()) {
      fprintf(stderr,"%s: array size of active_info and non-active info not equal.\n", __func__);
      goto out;
   }

   if(active_info.size()) {
      printf("%s: Got the corresponding disk path from the partition name, and stored them into arrays.\n", __func__);
      return 0;
   } else {
      printf("%s: failed to fill the arrays.\n", __func__);
      goto out;
   }

out:
   return ret;
}

int libabctl_getPartitionIndexForSlot(int slot, char* device)
{
   int ret = -1;
   const char *disk_node = NULL;

   assert(slot <= MAX_SLOTS);

   const char16_t* boot_partition = slot == 0 ? u"boot_a" : u"boot_b";

   disk_node = get_disk_from_part(boot_partition);
   if(NULL == disk_node)
      return -1;

   ret = libgpt_getPartitionEntry(disk_node, boot_partition, NULL);

   return ret;
}

int libabctl_getBootSlot()
{
   int active_slot = -1;
   int fd = -1;
   ssize_t read_bytes = 0;
   char *key_val_pair = NULL;
   char *saveptr = NULL;
   char cmdline[CMDLINE_LEN + 1] = {0};

   do {
      fd = open(KERNEL_CMDLINE, O_RDONLY, 0);
      if ( fd == -1) {
         perror("libabctl: open: KERNEL_CMDLINE ");
         break;
      }

      read_bytes = read(fd, cmdline, CMDLINE_LEN);
      if ( read_bytes == -1) {
         perror("libabctl: read: KERNEL_CMDLINE ");
         close(fd);
         break;
      }

      cmdline[read_bytes] = '\0';
      close(fd);

      // Ensure cmdline is null-terminated
      if (read_bytes >= CMDLINE_LEN) {
         read_bytes = CMDLINE_LEN - 1;
      }
      cmdline[read_bytes] = '\0';

      key_val_pair = strtok_r(cmdline, " ", &saveptr);

      while (key_val_pair != NULL) {
         if (!(strncmp(key_val_pair, "androidboot.slot_suffix=_",
                       SLOT_SUFFIX_STRLEN))) {
            switch(key_val_pair[SLOT_SUFFIX_STRLEN]) {
               case 'a': {
                  active_slot = 0;
                  break;
               }

               case 'b': {
                  active_slot = 1;
                  break;
               }

               default:
                  fprintf(stderr,"Unknown slot\n");
            }
         }
         key_val_pair = strtok_r(NULL, " ", &saveptr);
      }
   } while (0);

   return active_slot;
}

int libabctl_SetBootSuccess()
{
   int ret = -1;
   int current_slot = libabctl_getBootSlot();
   if(current_slot == -1) {
      fprintf(stderr, "%s: fail to get boot slot\n", __func__);
      return ret;
   }
   assert(current_slot <= MAX_SLOTS);
#ifdef LVMISC_PARTITION
   lvmisc_partition_t lvmisc_partition;
   if (lvmisc_read(lvmisc_partition) == -1) {
      fprintf(stderr,"%s: failed to read the lv_misc partition", __func__);
      goto out;
   }
   printf("%s: Before lvgvm libabctl_SetBootSuccess\n", __func__);
   lvmisc_print_lvmisc(lvmisc_partition);
   /* Update lvmisc_partition */
   if(current_slot == 0) { // current slot = a
      lvmisc_partition.reserved[0] = 'a'; // gvm current_slot 'a'
      lvmisc_partition.reserved[1] = 'a'; // gvm target_slot  'a'
      lvmisc_partition.reserved[2] = 'y';// gvm bootable status of target slot
      lvmisc_partition.reserved[3] = 'y';// gvm Slot0 Mark boot successful status
   } else if (current_slot == 1) {// current slot = b
      lvmisc_partition.reserved[0] = 'b'; // gvm current_slot 'b'
      lvmisc_partition.reserved[1] = 'b'; // gvm target_slot  'a'
      lvmisc_partition.reserved[2] = 'y';// gvm bootable status of target slot
      lvmisc_partition.reserved[5] = 'y';// gvm Slot1 Mark boot successful status
   }

   if (lvmisc_write(lvmisc_partition) == -1) {
      fprintf(stderr, "%s: Failed to write the lv_misc partition.\n", __func__);
      goto out;
   }

   lvmisc_partition_t lvmisc_partition_verify;
   if (lvmisc_read(lvmisc_partition_verify) == -1) {
      fprintf(stderr,"%s: failed to read the updated lv_misc partition", __func__);
      goto out;
   }
   printf("%s: After lvgvm libabctl_SetBootSuccess\n", __func__);
   lvmisc_print_lvmisc(lvmisc_partition_verify);
   ret = 0;
#else
   gpt_partition_entry_t e;
   char16_t *boot_partition = get_partition_name(current_slot);
   const char *disk_node = get_disk_from_part(boot_partition);

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

   ret = libgpt_setAttribute(disk_node, boot_partition,
                                             e.attribute_flags);
#endif
out:
   return ret;
}

int libabctl_getActiveStatus(unsigned int slot)
{
   int ret = -1;
   assert(slot <= MAX_SLOTS);
#ifdef LVMISC_PARTITION
   lvmisc_partition_t lvmisc_partition;
   if (lvmisc_read(lvmisc_partition) == -1) {
      fprintf(stderr, "%s: Failed to read the lv_misc partition.\n", __func__);
      goto out;
   }
   if (slot == 0) {
      ret = (lvmisc_partition.reserved[1] == 'a') ? 1 : 0;
   }
   else if (slot == 1) {
      ret = (lvmisc_partition.reserved[1] == 'b') ? 1 : 0;
   }
   else {
      ret = (lvmisc_partition.reserved[0] == 'a') ? 0 : 1;
   }
#else
   gpt_partition_entry_t e;
   const char16_t* boot_partition = (slot == 0 ? u"boot_a" : u"boot_b");
   const char *disk_node = get_disk_from_part(boot_partition);

   if(NULL == disk_node)
      goto out;

   ret = libgpt_getPartitionEntry(disk_node, boot_partition, &e);
   if(ret == -1) {
      perror("libabctl_getActiveStatus: ");
      goto out;
   }

   ret = (e.attribute_flags & PARTITION_ATTRIBUTE_ACTIVE_VAL)? 1 : 0;
#endif
out:
   return ret;
}

/*  1. For legacy non-atomic A/B
    a. Update the attr flag (active/sucessful/unbootable) for boot partition only.
    b. Swap the GUID of the partitions.
    c. UFS: Set UFS flag for XBL/ EMMC: Set boot partition enable bits.

2. For atomic A/B
    a. Update the attr flag (active/sucessful/unbootable) for boot partition only.
    b. UFS: Set UFS flag for XBL/ EMMC: Set boot partition enable bits.
*/
int libabctl_setActive(unsigned int slot)
{
   int ret = -1;
   assert(slot <= MAX_SLOTS);

#ifdef LVMISC_PARTITION
   lvmisc_partition_t lvmisc_partition;
   int current_slot = libabctl_getBootSlot();
   if (lvmisc_read(lvmisc_partition) == -1) {
         fprintf(stderr,"%s: failed to read the lv_misc partition", __func__);
         goto out;
   }
   printf("%s: Before lvgvm libabctl_setActive\n", __func__);
   lvmisc_print_lvmisc(lvmisc_partition);
   /* Update lvmisc_partition */
   if((current_slot == 0) && (slot == 0)) { // current slot = a, target slot = a
      lvmisc_partition.reserved[0] = 'a'; // gvm current_slot 'a'
      lvmisc_partition.reserved[1] = 'a'; // gvm target_slot  'a'
      lvmisc_partition.reserved[2] = '\0';// gvm bootable status of target slot
      lvmisc_partition.reserved[3] = '\0';// gvm Slot0 Mark boot successful status
      lvmisc_partition.reserved[4] = '\0';// gvm Slot0 Set unBootable Status
   } else if ((current_slot == 0) && (slot == 1)) {// current slot = a, target slot = b
      lvmisc_partition.reserved[0] = 'a'; // gvm current_slot 'a'
      lvmisc_partition.reserved[1] = 'b'; // gvm target_slot  'b'
      lvmisc_partition.reserved[2] = '\0';// gvm bootable status of target slot
      lvmisc_partition.reserved[5] = '\0';// gvm Slot1 Mark boot successful status
      lvmisc_partition.reserved[6] = '\0';// gvm Slot1 Set unBootable Status
   } else if ((current_slot == 1) && (slot == 0)) {// current slot = b, target slot = a
      lvmisc_partition.reserved[0] = 'b'; // gvm current_slot 'b'
      lvmisc_partition.reserved[1] = 'a'; // gvm target_slot  'a'
      lvmisc_partition.reserved[2] = '\0';// gvm bootable status of target slot
      lvmisc_partition.reserved[3] = '\0';// gvm Slot0 Mark boot successful status
      lvmisc_partition.reserved[4] = '\0';// gvm Slot0 Set unBootable Status
   } else if ((current_slot == 1) && (slot == 1)) {// current slot = b, target slot = b
      lvmisc_partition.reserved[0] = 'b'; // gvm current_slot 'b'
      lvmisc_partition.reserved[1] = 'b'; // gvm target_slot  'b'
      lvmisc_partition.reserved[2] = '\0';// gvm bootable status of target slot
      lvmisc_partition.reserved[5] = '\0';// gvm Slot1 Mark boot successful status
      lvmisc_partition.reserved[6] = '\0';// gvm Slot1 Set unBootable Status
   }

   if (lvmisc_write(lvmisc_partition) == -1) {
      fprintf(stderr, "%s: Failed to write the lv_misc partition.\n", __func__);
      goto out;
   }

   lvmisc_partition_t lvmisc_partition_verify;
   if (lvmisc_read(lvmisc_partition_verify) == -1) {
      fprintf(stderr,"%s: failed to read the updated lv_misc partition", __func__);
      goto out;
   }
   printf("%s: After lvgvm libabctl_setActive\n", __func__);
   lvmisc_print_lvmisc(lvmisc_partition_verify);
   return 0;
#else

   int iter = 0;
   struct stat st;

   int boot_type = get_boot_dev_type();

   if(TYPE_ERROR == boot_type)
      goto out;

   if(slot == libabctl_getBootSlot()) {
      printf("%s: the current active slot is same as the slot to be set active.\n", __func__);
      return 0;
   }

   ret = get_info_from_part_list(slot);
   if(ret == -1) {
      fprintf(stderr,"%s: failed to store all partitions into arrays.", __func__);
      goto out;
   }

   /*Iterate through all the partitons that are impacted by AB*/
   for( iter = 0; iter < active_info.size(); iter++) {

      /* skip xbl_x & xbl_config_x which are inside UFS boot partition. */
      if (boot_type == TYPE_UFS &&
        (!strcmp16(active_info[iter].partition, u"xbl_a") ||
        !strcmp16(active_info[iter].partition, u"xbl_b") ||
        !strcmp16(active_info[iter].partition, u"xbl_config_a")||
        !strcmp16(active_info[iter].partition, u"xbl_config_b")))
      {
        continue;
      }

      /* Update attribute only for boot partition. */
      if (!strcmp16(active_info[iter].partition, u"boot_a") ||
          !strcmp16(active_info[iter].partition, u"boot_b"))
      {
         ret = update_partition_attr(active_info[iter].disk, active_info[iter].partition, 1);
         if(ret == -1) {
            fprintf(stderr,"%s: failed to update active boot partition attribute", __func__);
            goto out;
         }

         ret = update_partition_attr(non_active_info[iter].disk, non_active_info[iter].partition, 0);
         if(ret == -1) {
            fprintf(stderr,"%s: failed to update non-active boot partition attribute", __func__);
            goto out;
         }

         printf("%s: Update attribute done.\n", __func__);
      }

#ifndef SUPPORT_ENABLE_LV_ATOMIC_AB
      /*Swap GUID */
      ret = libgpt_setTypeGUID(active_info[iter].disk, active_info[iter].partition,
                                   &(non_active_info[iter].partition_typeguid));
       if(ret == -1) {
         fprintf(stderr,"%s: failed to update active GPT", __func__);
          goto out;
       }

      ret = libgpt_setTypeGUID(non_active_info[iter].disk, non_active_info[iter].partition,
                                   &(active_info[iter].partition_typeguid));
      if(ret == -1) {
         fprintf(stderr,"%s: failed to update non-active GPT\n", __func__);
         goto out;
      }
      printf("%s: Swap GUID done.\n", __func__);
#endif
   }
   if(TYPE_UFS == boot_type) {
      if(stat(XBL_AB_PRIMARY, &st) || stat(XBL_AB_SECONDARY, &st))
         fprintf(stderr, "%s: UFS: xbl part not found(%s). Assuming sbl in use\n", __func__, strerror(errno));
      else {
         ret = set_xbl_boot_partition(slot);
         if(ret)
             fprintf(stderr, "%s: UFS: failed to set boot lun\n", __func__);
      }
   }

   if (TYPE_EMMC == boot_type) {
      // Determine the boot partition based on the slot
      int boot_partition = (slot == 1) ? ABCTL_EXT_CSD_PART_CONFIG_ACC_BOOT0 : ABCTL_EXT_CSD_PART_CONFIG_ACC_USER_AREA;
      int send_ack = 1; // Assuming send_ack is always 1

      // Set boot partition enable bits
      ret = do_write_boot_en(boot_partition, send_ack);
      if (ret) {
         fprintf(stderr, "%s: EMMC: failed to set boot partition\n", __func__);
      }
   }
#endif
out:
   return ret;
}

int libabctl_getSuccessStatus(unsigned int slot)
{
   int ret = -1;
#ifdef LVMISC_PARTITION
   lvmisc_partition_t lvmisc_partition;
   int current_slot = libabctl_getBootSlot();
   if (lvmisc_read(lvmisc_partition) == -1) {
         fprintf(stderr,"%s: failed to read the lv_misc partition", __func__);
         goto out;
   }
   if ((slot == 0) && (lvmisc_partition.reserved[3] == 'y') && (lvmisc_partition.reserved[2] == 'y')) {
      lvmisc_partition.reserved[2] = 'n';
      lvmisc_partition.reserved[3] = 'n';
      ret = lvmisc_write(lvmisc_partition);
      if(ret == -1) {
         fprintf(stderr,"%s: failed to write the lv_misc partition", __func__);
         goto out;
      }
      printf("%s: slot = 0, and is marked as successful = y\n", __func__);
      return 1;
   }
   if ((slot == 1) && (lvmisc_partition.reserved[5] == 'y') && (lvmisc_partition.reserved[2] == 'y')) {
      lvmisc_partition.reserved[2] = 'n';
      lvmisc_partition.reserved[5] = 'n';
      ret = lvmisc_write(lvmisc_partition);
      if(ret == -1) {
         fprintf(stderr,"%s: failed to write the lv_misc partition", __func__);
         goto out;
      }
      printf("%s: slot = 1, and is marked as successful = y\n", __func__);
      return 1;
   }
   printf("%s: slot = %d, and is marked as successful = n\n", __func__);

#else
   gpt_partition_entry_t e;
   const char16_t* boot_partition = (slot == 0 ? u"boot_a" : u"boot_b");
   const char *disk_node = get_disk_from_part(boot_partition);

   if(NULL == disk_node)
      goto out;

   ret = libgpt_getPartitionEntry(disk_node, boot_partition, &e);
   if(ret == -1) {
      perror("libabctl_getSuccessStatus: ");
      goto out;
   }

   ret = (e.attribute_flags & PARTITION_ATTRIBUTE_SUCCESSFUL_VAL) ? 1 : 0;
#endif
out:
   return ret;
}

int libabctl_setUnbootable(unsigned int slot)
{
   int ret = -1;
#ifdef LVMISC_PARTITION
   lvmisc_partition_t lvmisc_partition;
   if (lvmisc_read(lvmisc_partition) == -1) {
         fprintf(stderr,"%s: failed to read the lv_misc partition", __func__);
         goto out;
   }
   if (slot == 0) {// gvm Slot0 Set unBootable Status  ('y' or 'n')
      printf("slot=0 and set_slot_unbootable reserved[4] = y\n");
      lvmisc_partition.reserved[4] = 'y';
   }
   if (slot == 1) {// gvm Slot1 Set unBootable Status  ('y' or 'n')
      printf("slot=1 and set_slot_unbootable reserved[6] = y");
      lvmisc_partition.reserved[6] = 'y';
   }
      if (lvmisc_write(lvmisc_partition) == -1) {
      fprintf(stderr, "%s: Failed to write the lv_misc partition.\n", __func__);
      goto out;
   }

   lvmisc_partition_t lvmisc_partition_verify;
   if (lvmisc_read(lvmisc_partition_verify) == -1) {
      fprintf(stderr,"%s: failed to read the updated lv_misc partition", __func__);
      goto out;
   }

   lvmisc_print_lvmisc(lvmisc_partition_verify);
   ret = 0;
#else
   char16_t *partition = get_partition_name(slot);
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
#endif
out:
   return ret;
}

void libabctl_debugPrints(int do_prints)
{
   libgpt_dbg_prints(do_prints);
}

int libabctl_getUnbootableStatus(unsigned int slot)
{
   int ret = -1;
#ifdef LV_MISC_PARTITION_ENABLED
   lvmisc_partition_t lvmisc_partition;
   if (lvmisc_read(lvmisc_partition) == -1) {
         fprintf(stderr,"%s: failed to read the lv_misc partition", __func__);
         goto out;
   }
   if ((slot == 0) && (lvmisc_partition.reserved[4] = 'y')) {// gvm Slot0 Set unBootable Status  ('y' or 'n')
      printf("slot=0 , _a is unbootable");
      return 1;
   }
   if ((slot == 1) && (lvmisc_partition.reserved[6] = 'y')) {// gvm Slot1 Set unBootable Status  ('y' or 'n')
      printf("slot=1 , _b is unbootable");
      return 1;
   }
   printf("slot=%d is bootable", slot);
   lvmisc_print_lvmisc(lvmisc_partition);
   ret = 0;
#else
   gpt_partition_entry_t e;
   const char16_t* boot_partition = (slot == 0 ? u"boot_a" : u"boot_b");
   const char *disk_node = get_disk_from_part(boot_partition);
   if(NULL == disk_node)
      goto out;
   ret = libgpt_getPartitionEntry(disk_node, boot_partition, &e);
   if(ret == -1) {
      perror("libabctl_getUnbootableStatus: ");
      goto out;
   }
   ret = (e.attribute_flags & PARTITION_ATTRIBUTE_UNBOOTABLE_VAL) ? 1 : 0;
#endif
out:
   return ret;
}
