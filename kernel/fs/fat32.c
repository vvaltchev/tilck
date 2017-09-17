
#include <common_defs.h>
#include <string_util.h>
#include <fs/fat32.h>

/*
 * *********************************************
 * DEBUG util code
 * *********************************************
 */

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


static void dump_fat16_headers(void *data)
{
   fat16_header2 *hdr = (void*) ( (u8*)data + sizeof(fat_header) );

   printk("Drive num: %u\n", hdr->BS_DrvNum);
   printk("BootSig: %u\n", hdr->BS_BootSig);
   printk("VolID: %p\n", hdr->BS_VolID);
   dump_fixed_str("VolLab", hdr->BS_VolLab, sizeof(hdr->BS_VolLab));
   dump_fixed_str("FilSysType", hdr->BS_FilSysType, sizeof(hdr->BS_FilSysType));
}

static void dump_entry_attrs(fat_entry *entry)
{
   printk("readonly:  %u\n", entry->readonly);
   printk("hidden:    %u\n", entry->hidden);
   printk("system:    %u\n", entry->system);
   printk("vol id:    %u\n", entry->volume_id);
   printk("directory: %u\n", entry->directory);
   printk("archive:   %u\n", entry->archive);
}

static int dump_directory(fat_header *hdr, fat_entry *entry, int level);


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


   char shortname[12];
   fat_get_short_name(entry, shortname);

   char indentbuf[4*16] = {0};
   for (int i = 0; i < 4*level; i++)
      indentbuf[i] = ' ';

   if (!entry->directory) {
      printk("%s%s: %u bytes\n", indentbuf, shortname, entry->DIR_FileSize);
   } else {
      printk("%s%s\n", indentbuf, shortname);
   }

   if (entry->directory) {
      fat_entry *e = fat_get_pointer_to_first_cluster(hdr, entry);
      dump_directory(hdr, e, level);
      return 0;
   }

   return 0;
}


static int dump_directory(fat_header *hdr, fat_entry *entry, int level)
{
   int ret;
   do {
      ret = dump_dir_entry(hdr, entry, level+1);
      entry++;
   } while (ret == 0);
   return 0;
}


void fat_dump_info(void *fatpart_begin)
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



/*
 * *********************************************
 * Actual FAT code
 * *********************************************
 */

fat_type fat_get_type(fat_header *hdr)
{
   u32 FATSz = fat_get_FATSz(hdr);
   u32 TotSec = fat_get_TotSec(hdr);
   u32 RootDirSectors = fat_get_RootDirSectors(hdr);
   u32 FatAreaSize = hdr->BPB_NumFATs * FATSz;
   u32 DataSec = TotSec - (hdr->BPB_RsvdSecCnt + FatAreaSize + RootDirSectors);
   u32 CountofClusters = DataSec / hdr->BPB_SecPerClus;

   if (CountofClusters < 4085) {

      /* Volume is FAT12 */
      return fat12_type;

   } else if (CountofClusters < 65525) {

      /* Volume is FAT16 */
      return fat16_type;

   } else {

      return fat32_type;
      /* Volume is FAT32 */
   }
}

/*
 * Reads the entry in the FAT 'fatNum' for cluster 'clusterN'.
 * The entry may be 16 or 32 bit. It returns 32-bit integer for convenience.
 */
u32 fat_read_fat_entry(fat_header *hdr, fat_type ft, int clusterN, int fatNum)
{
   if (ft == fat_unknown) {
      ft = fat_get_type(hdr);
   }

   if (ft == fat12_type) {
      // FAT12 is NOT supported.
      NOT_REACHED();
   }

   ASSERT(fatNum < hdr->BPB_NumFATs);

   u32 FATSz = fat_get_FATSz(hdr);
   u32 FATOffset = (ft == fat16_type) ? clusterN * 2 : clusterN * 4;

   u32 ThisFATSecNum =
      fatNum * FATSz + hdr->BPB_RsvdSecCnt + (FATOffset / hdr->BPB_BytsPerSec);

   u32 ThisFATEntOffset = FATOffset % hdr->BPB_BytsPerSec;

   u8 *SecBuf = (u8*)hdr + ThisFATSecNum * hdr->BPB_BytsPerSec;

   if (ft == fat16_type) {
      return *(u16*)(SecBuf+ThisFATEntOffset);
   }

   // FAT32
   // Note: FAT32 "FAT" entries are 28-bit. The 4 higher bits are reserved.
   return (*(u32*)(SecBuf+ThisFATEntOffset)) & 0x0FFFFFFF;
}

int fat_get_sector_for_cluster(fat_header *hdr, int N)
{
   int RootDirSectors = fat_get_RootDirSectors(hdr);

   int FirstDataSector = hdr->BPB_RsvdSecCnt +
      (hdr->BPB_NumFATs * hdr->BPB_FATSz16) + RootDirSectors;

   // FirstSectorofCluster
   return ((N - 2) * hdr->BPB_SecPerClus) + FirstDataSector;
}

fat_entry *fat_get_rootdir(fat_header *hdr)
{
   int first_sec =
      hdr->BPB_RsvdSecCnt + (hdr->BPB_NumFATs * hdr->BPB_FATSz16);

   return (fat_entry*) ((u8*)hdr + (hdr->BPB_BytsPerSec * first_sec));
}

void *
fat_get_pointer_to_first_cluster(fat_header *hdr, fat_entry *entry)
{
   int first_cluster = entry->DIR_FstClusHI << 16 | entry->DIR_FstClusLO;
   int sector = fat_get_sector_for_cluster(hdr, first_cluster);
   return ((u8*)hdr + sector * hdr->BPB_BytsPerSec);
}

void fat_get_short_name(fat_entry *entry, char *destbuf)
{
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

   int d = 0;
   char *ptr = fn;
   while (*ptr) {
      destbuf[d++] = *ptr++;
   }

   if (ext[0] == 0) {
      destbuf[d++] = 0;
      return;
   }

   destbuf[d++] = '.';
   ptr = ext;

   while (*ptr) {
      destbuf[d++] = *ptr++;
   }

   destbuf[d++] = 0;
}


static fat_entry *
fat_search_entry_int(fat_header *hdr, fat_entry *entry, const char *path)
{
   char comp[256];
   int complen = 0;

bigloop:

   if (*path == 0)
      return NULL;

   while (*path && *path != '/') {
      comp[complen++] = *path;
      path++;
   }
   comp[complen++] = 0;

   //printk("comp is '%s'\n", comp);

   while (true) {

      // that means all the rest of the entries are free.
      if (entry->sfname[0] == 0) {
         break;
      }

      // that means that the directory is empty
      if (entry->sfname[0] == (char)0xE5) {
         break;
      }

      // '.' is NOT a legal char in the short name
      // With this check, we skip the directories '.' and '..'.
      if (entry->sfname[0] == '.') {
         entry++;
         continue;
      }

      char shortname[12];
      fat_get_short_name(entry, shortname);

      //printk("checking entry with sname '%s'\n", shortname);

      if (!stricmp(shortname, comp)) {

         // ok, we found the component.
         // if path is ended, that's it.

         if (*path == 0)
            return entry;

         // otherwise, *path is '/'
         ASSERT(*path == '/');

         path++;

         // path ended with '/'. that's still OK.
         if (*path == 0)
            return entry;

         // path continues.
         // if the current entry is not a directory, we failed.
         if (!entry->directory)
            break;

         entry = fat_get_pointer_to_first_cluster(hdr, entry);
         complen = 0;
         goto bigloop;
      }

      entry++;
   }

   return NULL;
}

fat_entry *fat_search_entry(fat_header *hdr, const char *abspath)
{
   if (*abspath != '/')
      return NULL;

   abspath++;

   fat_entry *root = fat_get_rootdir(hdr);

   if (*abspath == 0)
      return root;

   return fat_search_entry_int(hdr, root, abspath);
}



