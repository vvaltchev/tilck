#include <common_defs.h>

typedef enum {

   fat12_type = 1,
   fat16_type = 2,
   fat32_type = 3

} fat_type;

typedef struct __attribute__(( packed )) {

   u8 BS_jmpBoot[3];
   s8 BS_OEMName[8];
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

} fat_header;


typedef struct __attribute__(( packed )) {

   u8 BS_DrvNum;
   u8 BS_Reserved1;
   u8 BS_BootSig;
   u32 BS_VolID;
   char BS_VolLab[11];
   char BS_FilSysType[8];

} fat16_header2;

typedef struct __attribute__(( packed )) {

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

} fat32_header2;


typedef struct __attribute__(( packed )) {

   s8 sfname[11];

   u8 readonly : 1; // lower-bit
   u8 hidden : 1;
   u8 system : 1;
   u8 volume_id : 1;
   u8 directory : 1;
   u8 archive : 1;
   u8 unused1 : 2;  // higher 2 bits

   u8 DIR_NTRes;
   u8 DIR_CrtTimeTenth;

   u8 unused2[6];

   u16 DIR_FstClusHI;

   u16 DIR_WrtTime;
   u16 DIR_WrtDate;

   u16 DIR_FstClusLO;
   u32 DIR_FileSize;

} fat_entry;

// DEBUG functions
void fat_dump_info(void *fatpart_begin);

// FAT INTERNALS ---------------------------------------------------------------

fat_entry *fat_get_rootdir(fat_header *hdr);
void fat_get_short_name(fat_entry *entry, char *destbuf);
void *fat_get_pointer_to_first_cluster(fat_header *hdr, fat_entry *entry);
fat_type fat_get_type(fat_header *hdr);

// FATSz is the number of sectors per FAT
static inline u32 fat_get_FATSz(fat_header *hdr)
{
   if (hdr->BPB_FATSz16 != 0)
      return hdr->BPB_FATSz16;

   fat32_header2 *h2 = (fat32_header2*)(hdr+1);
   return h2->BPB_FATSz32;
}

static inline u32 fat_get_TotSec(fat_header *hdr)
{
   return hdr->BPB_TotSec16 != 0 ? hdr->BPB_TotSec16 : hdr->BPB_TotSec32;
}

static inline u32 fat_get_RootDirSectors(fat_header *hdr)
{
   u32 bps = hdr->BPB_BytsPerSec;
   return ((hdr->BPB_RootEntCnt * 32) + (bps - 1)) / bps;
}


// PUBLIC interface ------------------------------------------------------------
fat_entry *fat_search_entry(fat_header *hdr, const char *abspath);
