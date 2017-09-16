
#include <common_defs.h>
#include <string_util.h>
#include <fs/fat32.h>

static void dump_fixed_str(const char *what, char *str, u32 len)
{
   char buf[len+1];
   buf[len]=0;
   memcpy(buf, str, len);
   printk("%s: '%s'\n", what, buf);
}

void fat_dump_common_header(void *data)
{
   fat_header *bpb = data;

   dump_fixed_str("EOM name", bpb->BS_OEMName, sizeof(bpb->BS_OEMName));
   printk("Bytes per sec: %u\n", bpb->BPB_BytsPerSec);
   printk("Sectors per cluster: %u\n", bpb->BPB_SecPerClus);
   printk("Reserved sectors count: %u\n", bpb->BPB_RsvdSecCnt);
   printk("Num FATs: %u\n", bpb->BPB_NumFATs);
   printk("Root ent count: %u\n", bpb->BPB_RootEntCnt);
   printk("Tot Sec 16: %u\n", bpb->BPB_TotSec16);
   printk("Media: %u\n", bpb->BPB_Media);
   printk("FATz16: %u\n", bpb->BPB_FATSz16);
   printk("Sectors per track: %u\n", bpb->BPB_SecPerTrk);
   printk("Num heads: %u\n", bpb->BPB_NumHeads);
   printk("Hidden sectors: %u\n", bpb->BPB_HiddSec);
   printk("Total Sec 32: %u\n", bpb->BPB_TotSec32);
}

typedef struct __attribute__(( packed )) {

   u8 BS_DrvNum;
   u8 BS_Reserved1;
   u8 BS_BootSig;
   u32 BS_VolID;
   s8 BS_VolLab[11];
   s8 BS_FilSysType[8];

} fat16_bpb;

static void dump_fat16_headers(void *data)
{
   fat16_bpb *hdr = (void*) ( (u8*)data + sizeof(fat_header) );

   printk("Drive num: %u\n", hdr->BS_DrvNum);
   printk("BootSig: %u\n", hdr->BS_BootSig);
   printk("VolID: %p\n", hdr->BS_VolID);
   dump_fixed_str("VolLab", hdr->BS_VolLab, sizeof(hdr->BS_VolLab));
   dump_fixed_str("FilSysType", hdr->BS_FilSysType, sizeof(hdr->BS_FilSysType));
}

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

int get_sector_for_cluster(fat_header *hdr, int N)
{
   int RootDirSectors =
    ((hdr->BPB_RootEntCnt * 32)
     + (hdr->BPB_BytsPerSec - 1)) / hdr->BPB_BytsPerSec;

   int FirstDataSector = hdr->BPB_RsvdSecCnt +
      (hdr->BPB_NumFATs * hdr->BPB_FATSz16) + RootDirSectors;

   int FirstSectorofCluster = ((N - 2) * hdr->BPB_SecPerClus) + FirstDataSector;
   return FirstSectorofCluster;
}

fat_entry *fat_get_rootdir(fat_header *hdr)
{
   int first_sec =
      hdr->BPB_RsvdSecCnt + (hdr->BPB_NumFATs * hdr->BPB_FATSz16);

   return (fat_entry*) ((u8*)hdr + (hdr->BPB_BytsPerSec * first_sec));
}

int dump_directory(fat_header *hdr, fat_entry *entry, int level);

int dump_dir_entry(fat_header *hdr, fat_entry *entry, int level)
{
   if (entry->volume_id) {
      return 0; // the first "file" is the volume ID. Skip it.
   }

   // that means all the rest of the entries are free.
   if (entry->sfname[0] == 0) {
      return -1;
   }

   // that means that the directory is empty
   if (entry->sfname[0] == (char)0xE5) {
      return -1;
   }

   // '.' is NOT a legal char in the short name
   // With this check, we skip the directories '.' and '..'.
   if (entry->sfname[0] == '.') {
      return 0;
   }

   char fn[32] = {0};
   for (int i = 0; i <= 8; i++) {
      if (entry->sfname[i] == ' ')
         break;
      fn[i] = entry->sfname[i];
   }

   char ext[4] = {0};
   for (int i = 8; i < 11; i++) {
      if (entry->sfname[i] == ' ')
         break;
      ext[i-8] = entry->sfname[i];
   }

   char indentbuf[4*16] = {0};
   for (int i = 0; i < 4*level; i++)
      indentbuf[i] = ' ';

   if (!entry->directory) {
      printk("%s%s%s%s: %u bytes\n", indentbuf, fn,
             ext[0] != 0 ? "." : "", ext, entry->DIR_FileSize);
   } else {
      printk("%s%s\n", indentbuf, fn);
   }

   // printk("readonly:  %u\n", entry->readonly);
   // printk("hidden:    %u\n", entry->hidden);
   // printk("system:    %u\n", entry->system);
   // printk("vol id:    %u\n", entry->volume_id);
   // printk("directory: %u\n", entry->directory);

   int first_cluster = entry->DIR_FstClusHI << 16 | entry->DIR_FstClusLO;

   if (entry->directory) {
      int sec = get_sector_for_cluster(hdr, first_cluster);
      fat_entry *e = (fat_entry*)((u8*)hdr + sec * hdr->BPB_BytsPerSec);
      dump_directory(hdr, e, level);
      return 0;
   }

   //printk("first cluster: %u\n", first_cluster);
   return 0;
}

int dump_directory(fat_header *hdr, fat_entry *entry, int level)
{
   int ret;
   do {
      ret = dump_dir_entry(hdr, entry, level+1);
      entry++;
   } while (ret == 0);
   return 0;
}



fat_entry *fat32_search_entry(fat_header *hdr, const char *abspath)
{
   if (*abspath != '/')
      return NULL;


   return NULL;
}

void fat32_dump_info(void *fatpart_begin)
{
   fat_header *hdr = fatpart_begin;
   fat_dump_common_header(fatpart_begin);

   printk("\n");

   if (hdr->BPB_TotSec16 != 0) {
      dump_fat16_headers(fatpart_begin);
   } else {
      printk("FAT32 not supported yet.\n");
   }
   printk("\n");

   fat_entry *root = fat_get_rootdir(hdr);
   dump_directory(hdr, root, 0);
}


