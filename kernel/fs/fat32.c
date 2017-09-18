
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


static void dump_fat16_headers(fat_header *common_hdr)
{
   fat16_header2 *hdr = (fat16_header2*) (common_hdr+1);

   printk("BS_DrvNum: %u\n", hdr->BS_DrvNum);
   printk("BS_BootSig: %u\n", hdr->BS_BootSig);
   printk("BS_VolID: %p\n", hdr->BS_VolID);
   dump_fixed_str("BS_VolLab", hdr->BS_VolLab, sizeof(hdr->BS_VolLab));
   dump_fixed_str("BS_FilSysType",
                  hdr->BS_FilSysType, sizeof(hdr->BS_FilSysType));
}

static void dump_fat32_headers(fat_header *common_hdr)
{
   fat32_header2 *hdr = (fat32_header2*) (common_hdr+1);
   printk("BPB_FATSz32: %u\n", hdr->BPB_FATSz32);
   printk("BPB_ExtFlags: %u\n", hdr->BPB_ExtFlags);
   printk("BPB_FSVer: %u\n", hdr->BPB_FSVer);
   printk("BPB_RootClus: %u\n", hdr->BPB_RootClus);
   printk("BPB_FSInfo: %u\n", hdr->BPB_FSInfo);
   printk("BPB_BkBootSec: %u\n", hdr->BPB_BkBootSec);
   printk("BS_DrvNum: %u\n", hdr->BS_DrvNum);
   printk("BS_BootSig: %u\n", hdr->BS_BootSig);
   printk("BS_VolID: %p\n", hdr->BS_VolID);
   dump_fixed_str("BS_VolLab", hdr->BS_VolLab, sizeof(hdr->BS_VolLab));
   dump_fixed_str("BS_FilSysType",
                  hdr->BS_FilSysType, sizeof(hdr->BS_FilSysType));
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

static int dump_dir_entry(fat_header *hdr,
                          fat_type ft,
                          fat_entry *entry,
                          void *arg,
                          int level)
{
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

      fat_walk_directory(hdr,
                         ft,
                         NULL,
                         fat_get_first_cluster(entry),
                         &dump_dir_entry,
                         arg,
                         level + 1);
      return 0;
   }

   return 0;
}


void fat_dump_info(void *fatpart_begin)
{
   fat_header *hdr = fatpart_begin;
   fat_dump_common_header(fatpart_begin);

   printk("\n");

   fat_type ft = fat_get_type(hdr);
   ASSERT(ft != fat12_type);

   if (ft == fat16_type) {
      dump_fat16_headers(fatpart_begin);
   } else {
      dump_fat32_headers(hdr);
   }
   printk("\n");

   u32 root_dir_cluster;
   fat_entry *root = fat_get_rootdir(hdr, ft, &root_dir_cluster);

   fat_walk_directory(hdr,
                      ft,
                      root,
                      root_dir_cluster,
                      &dump_dir_entry, // callback
                      NULL,            // arg
                      0);              // level
}



/*
 * *********************************************
 * Actual FAT code
 * *********************************************
 */

int fat_walk_directory(fat_header *hdr,
                       fat_type ft,
                       fat_entry *entry,
                       u32 cluster,
                       fat_dentry_cb cb,
                       void *arg,
                       int level)
{
   const u32 entries_per_cluster =
      (hdr->BPB_BytsPerSec * hdr->BPB_SecPerClus) / sizeof(fat_entry);

   ASSERT(ft == fat16_type || ft == fat32_type);

   while (true) {

      if (cluster != 0) {

         /*
          * if cluster != 0, cluster is used and entry is overriden.
          * That's because on FAT16 we know only the sector of the root dir.
          * In that case, fat_get_rootdir() returns 0 as cluster. In all the
          * other cases, we need only the cluster.
          */
         entry = fat_get_pointer_to_cluster_data(hdr, cluster);
      }

      for (u32 i = 0; i < entries_per_cluster; i++) {

         if (entry[i].volume_id) {
            continue; // the first "file" is the volume ID. Skip it.
         }

         // that means all the rest of the entries are free.
         if (entry[i].DIR_Name[0] == 0) {
            return 0;
         }

         // that means that the directory is empty
         if (entry[i].DIR_Name[0] == (char)0xE5) {
            return 0;
         }

         /*
          * '.' is NOT a legal char in the short name. Therefore, with this
          * simple check, we skip the directories '.' and '..'.
          */
         if (entry[i].DIR_Name[0] == '.') {
            continue;
         }

         int ret = cb(hdr, ft, entry + i, arg, level);

         // if ret < 0, we have to stop the walk.
         if (ret < 0)
            return 0;
      }

      /*
       * In case dump_directory() has been called on the root dir on a FAT16,
       * cluster is 0 (invalid) and there is no next cluster in the chain. This
       * fact seriously limits the number of items in the root dir of a FAT16
       * volume.
       */
      if (cluster == 0)
         break;

      /*
       * If we're here, it means that there is more then one cluster for the
       * entries of this directory. We have to follow the chain.
       */
      u32 val = fat_read_fat_entry(hdr, ft, cluster, 0);

      if (fat_is_end_of_clusterchain(ft, val))
         break; // that's it: we hit an exactly full cluster.

      // we do not expect BAD CLUSTERS
      ASSERT(!fat_is_bad_cluster(ft, val));

      cluster = val;
   }

   return 0;
}

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

u32 fat_get_sector_for_cluster(fat_header *hdr, u32 N)
{
   u32 RootDirSectors = fat_get_RootDirSectors(hdr);

   u32 FirstDataSector = hdr->BPB_RsvdSecCnt +
      (hdr->BPB_NumFATs * hdr->BPB_FATSz16) + RootDirSectors;

   // FirstSectorofCluster
   return ((N - 2) * hdr->BPB_SecPerClus) + FirstDataSector;
}

fat_entry *fat_get_rootdir(fat_header *hdr, fat_type ft, u32 *cluster /* out */)
{
   ASSERT(ft != fat12_type);
   ASSERT(ft != fat_unknown);

   u32 sector;

   if (ft == fat16_type) {

      u32 FirstDataSector =
         hdr->BPB_RsvdSecCnt + (hdr->BPB_NumFATs * hdr->BPB_FATSz16);

      sector = FirstDataSector;
      *cluster = 0;

   } else {

      // FAT32
      fat32_header2 *h32 = (fat32_header2*) (hdr+1);
      *cluster = h32->BPB_RootClus;
      sector = fat_get_sector_for_cluster(hdr, *cluster);
   }

   return (fat_entry*) ((u8*)hdr + (hdr->BPB_BytsPerSec * sector));
}

void fat_get_short_name(fat_entry *entry, char *destbuf)
{
   char fn[32] = {0};
   for (int i = 0; i <= 8; i++) {
      if (entry->DIR_Name[i] == ' ')
         break;
      fn[i] = entry->DIR_Name[i];
   }

   char ext[4] = {0};
   for (int i = 8; i < 11; i++) {
      if (entry->DIR_Name[i] == ' ')
         break;
      ext[i-8] = entry->DIR_Name[i];
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

// TODO: make the code to follow the clusterchain!!
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
      if (entry->DIR_Name[0] == 0) {
         break;
      }

      // that means that the directory is empty
      if (entry->DIR_Name[0] == (char)0xE5) {
         break;
      }

      // '.' is NOT a legal char in the short name
      // With this check, we skip the directories '.' and '..'.
      if (entry->DIR_Name[0] == '.') {
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

         u32 first_cluster = fat_get_first_cluster(entry);
         entry = fat_get_pointer_to_cluster_data(hdr, first_cluster);
         complen = 0;
         goto bigloop;
      }

      entry++;
   }

   return NULL;
}


fat_entry *fat_search_entry(fat_header *hdr, fat_type ft, const char *abspath)
{
   if (*abspath != '/')
      return NULL;

   if (ft == fat_unknown) {
      ft = fat_get_type(hdr);
   }

   abspath++;

   u32 root_dir_cluster;
   fat_entry *root = fat_get_rootdir(hdr, ft, &root_dir_cluster);

   if (*abspath == 0)
      return root;

   return fat_search_entry_int(hdr, root, abspath);
}

void
fat_read_whole_file(fat_header *hdr,
                    fat_entry *entry, char *dest_buf, size_t dest_buf_size)
{
   ASSERT(entry->DIR_FileSize <= dest_buf_size);

   // cluster size in bytes
   const u32 cs = hdr->BPB_SecPerClus * hdr->BPB_BytsPerSec;

   u32 cluster;
   size_t written = 0;
   size_t fsize = entry->DIR_FileSize;

   fat_type ft = fat_get_type(hdr);

   cluster = fat_get_first_cluster(entry);

   do {

      char *data = fat_get_pointer_to_cluster_data(hdr, cluster);

      size_t rem = fsize - written;

      if (rem <= cs) {
         // read what is needed
         memmove(dest_buf + written, data, rem);
         written += rem;
         break;
      }

      // read the whole cluster
      memmove(dest_buf + written, data, cs);
      written += cs;

      ASSERT((fsize - written) > 0);

      // find the new cluster
      u32 fatval = fat_read_fat_entry(hdr, ft, cluster, 0);

      if (fat_is_end_of_clusterchain(ft, fatval)) {
         // rem is still > 0, this should NOT be the last cluster
         NOT_REACHED();
      }

      // we do not expect BAD CLUSTERS
      ASSERT(!fat_is_bad_cluster(ft, fatval));

      cluster = fatval; // go reading the new cluster in the chain.

   } while (written < fsize);
}

