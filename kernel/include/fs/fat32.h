#include <common_defs.h>

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

void fat32_dump_info(void *fatpart_begin);
fat_entry *fat_search_entry(fat_header *hdr, const char *abspath);

void *
fat_get_pointer_to_first_cluster(fat_header *hdr, fat_entry *entry);
