/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>

enum fat_type {

   fat_unknown = 0, // unknown FAT type
   fat12_type  = 1,
   fat16_type  = 2,
   fat32_type  = 3
};

struct fat_hdr {

   u8 BS_jmpBoot[3];
   char BS_OEMName[8];
   u16 BPB_BytsPerSec;
   u8 BPB_SecPerClus;
   u16 BPB_RsvdSecCnt;
   u8 BPB_NumFATs;
   u16 BPB_RootEntCnt;
   u16 BPB_TotSec16;
   u8 BPB_Media;
   u16 BPB_FATSz16;
   u16 BPB_SecPerTrk;
   u16 BPB_NumHeads;
   u32 BPB_HiddSec;
   u32 BPB_TotSec32;

} PACKED;

struct fat16_header2 {

   u8 BS_DrvNum;
   u8 BS_Reserved1;
   u8 BS_BootSig;
   u32 BS_VolID;
   char BS_VolLab[11];
   char BS_FilSysType[8];

} PACKED;

struct fat32_header2 {

   u32 BPB_FATSz32;
   u16 BPB_ExtFlags;
   u16 BPB_FSVer;
   u32 BPB_RootClus;
   u16 BPB_FSInfo;
   u16 BPB_BkBootSec;
   u8 BPB_Reserved[12];
   u8 BS_DrvNum;
   u8 BS_Reserved1;
   u8 BS_BootSig;
   u32 BS_VolID;
   char BS_VolLab[11];
   char BS_FilSysType[8];

} PACKED;

/*
 * Special flags in DIR_NTRes telling us if the base part or the extention of
 * a short name is entirely in lower case.
 */

#define FAT_ENTRY_NTRES_BASE_LOW_CASE  0x08
#define FAT_ENTRY_NTRES_EXT_LOW_CASE   0x10

/* In case an extact comparison using DIR_Name is needed */
#define FAT_DIR_DOT      ".          "
#define FAT_DIR_DOT_DOT  "..         "

struct fat_entry {

   char DIR_Name[11];

   u8 readonly : 1;     // lower-bit
   u8 hidden : 1;
   u8 system : 1;
   u8 volume_id : 1;
   u8 directory : 1;
   u8 archive : 1;
   u8 resbit1 : 1;      // reserved bit
   u8 resbit2 : 1;      // reserved bit

   u8 DIR_NTRes;        // reserved to be used by Windows NT

   u8 DIR_CrtTimeTenth; // creation time, tenth of seconds (0-199)
   u16 DIR_CrtTime;     // creation time, granularity: 2 seconds
   u16 DIR_CrtDate;     // creation date
   u16 DIR_LstAccDate;  // last access date

   u16 DIR_FstClusHI;   // high word of entry's first cluster
   u16 DIR_WrtTime;     // time of last write
   u16 DIR_WrtDate;     // date of last write
   u16 DIR_FstClusLO;   // low word of entry's first cluster
   u32 DIR_FileSize;

} PACKED;

#define LAST_LONG_ENTRY_MASK (0x40)

struct fat_long_entry {

   u8 LDIR_Ord;
   u8 LDIR_Name1[10];
   u8 LDIR_Attr;
   u8 LDIR_Type; /* 0 means sub-comp of a long name entry. != 0 reserved. */
   u8 LDIR_Chksum;
   u8 LDIR_Name2[12];
   u16 LDIR_FstClusLO; /* must be always 0 in this context. */
   u8 LDIR_Name3[4];

} PACKED;


static inline bool is_long_name_entry(struct fat_entry *e)
{
   return e->readonly && e->hidden && e->system && e->volume_id;
}

// DEBUG functions
void fat_dump_info(void *fatpart_begin);

// FAT INTERNALS ---------------------------------------------------------------

enum fat_type fat_get_type(struct fat_hdr *hdr);

struct fat_entry *fat_get_rootdir(struct fat_hdr *hdr,
                           enum fat_type ft,
                           u32 *cluster /*out*/);

void fat_get_short_name(struct fat_entry *entry, char *destbuf);

u32 fat_get_sector_for_cluster(struct fat_hdr *hdr, u32 N);

u32 fat_read_fat_entry(struct fat_hdr *hdr,
                       enum fat_type ft,
                       u32 clusterN,
                       u32 fatNum);

u32 fat_get_first_data_sector(struct fat_hdr *hdr);


static inline u32 fat_get_reserved_sectors_count(struct fat_hdr *hdr)
{
   return hdr->BPB_RsvdSecCnt;
}

static inline u32 fat_get_sector_size(struct fat_hdr *hdr)
{
   return hdr->BPB_BytsPerSec;
}

// FATSz is the number of sectors per FAT
static inline u32 fat_get_FATSz(struct fat_hdr *hdr)
{
   if (hdr->BPB_FATSz16 != 0)
      return hdr->BPB_FATSz16;

   struct fat32_header2 *h2 = (struct fat32_header2*)(hdr+1);
   return h2->BPB_FATSz32;
}

static inline u32 fat_get_TotSec(struct fat_hdr *hdr)
{
   return hdr->BPB_TotSec16 != 0 ? hdr->BPB_TotSec16 : hdr->BPB_TotSec32;
}

static inline u32 fat_get_RootDirSectors(struct fat_hdr *hdr)
{
   u32 bps = hdr->BPB_BytsPerSec;
   return ((hdr->BPB_RootEntCnt * 32u) + (bps - 1u)) / bps;
}

static inline u32 fat_get_first_cluster(struct fat_entry *entry)
{
   return (u32)entry->DIR_FstClusHI << 16u | entry->DIR_FstClusLO;
}

static inline bool fat_is_end_of_clusterchain(enum fat_type ft, u32 val)
{
   ASSERT(ft == fat16_type || ft == fat32_type);
   return (ft == fat16_type) ? val >= 0xFFF8 : val >= 0x0FFFFFF8;
}

static inline bool fat_is_bad_cluster(enum fat_type ft, u32 val)
{
   ASSERT(ft == fat16_type || ft == fat32_type);
   return (ft == fat16_type) ? (val == 0xFFF7) : (val == 0x0FFFFFF7);
}

static inline void *
fat_get_pointer_to_cluster_data(struct fat_hdr *hdr, u32 clusterN)
{
   u32 sector = fat_get_sector_for_cluster(hdr, clusterN);
   return ((u8*)hdr + sector * hdr->BPB_BytsPerSec);
}

// PUBLIC interface ------------------------------------------------------------

bool fat32_is_valid_filename_character(char c);

struct fat_walk_dir_ctx {

   bool is_valid;       /* long name valid ? */
   u8 lname_buf[256];   /* long name buffer */
   s16 lname_sz;        /* long name size */
   s16 lname_chksum;    /* long name checksum */
};

typedef int (*fat_dentry_cb)(struct fat_hdr *,
                             enum fat_type,
                             struct fat_entry *,
                             const char *, /* long name */
                             void *);      /* user data pointer */

int
fat_walk_directory(struct fat_walk_dir_ctx *ctx,
                   struct fat_hdr *hdr,
                   enum fat_type ft,
                   struct fat_entry *entry,
                   u32 cluster,
                   fat_dentry_cb cb,
                   void *arg);


struct fat_entry *
fat_search_entry(struct fat_hdr *hdr,
                 enum fat_type ft,
                 const char *abspath,
                 int *err);

size_t fat_get_file_size(struct fat_entry *entry);

void
fat_read_whole_file(struct fat_hdr *hdr,
                    struct fat_entry *entry,
                    char *dest_buf,
                    size_t dest_buf_size);

u32 fat_get_used_bytes(struct fat_hdr *hdr);

// IMPLEMENTATION INTERNALS --------------------------------------------------

struct fat_search_ctx {

   // Input fields
   const char *path;          // the searched path.
   bool single_comp;          // search only for the first component

   // Output fields
   struct fat_entry *result;  // the found entry or NULL
   u32 subdir_cluster;        // the cluster of the subdir's we have to walk to
   bool not_dir;              // path ended with '/' but entry was NOT a dir

   // Internal fields
   char pc[256];              // path component
   size_t pcl;                // path component's length
   char shortname[16];        // short name of the current entry
   struct fat_walk_dir_ctx walk_ctx; // walk context: contains long names
};

void
fat_init_search_ctx(struct fat_search_ctx *ctx, const char *path, bool single_comp);

int fat_search_entry_cb(struct fat_hdr *hdr,
                        enum fat_type ft,
                        struct fat_entry *entry,
                        const char *long_name,
                        void *arg);
