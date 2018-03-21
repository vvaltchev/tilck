
#include <common_defs.h>
#include <string_util.h>
#include <fs/fat32.h>

#include <kmalloc.h>
#include <exos_errno.h>

/*
 * The following code uses in many cases the CamelCase naming convention
 * because it is based on the Microsoft's public document:
 *
 *    Microsoft Extensible Firmware Initiative
 *    FAT32 File System Specification
 *
 *    FAT: General Overview of On-Disk Format
 *
 *    Version 1.03, December 6, 2000
 *
 * Keeping the exact same names as the official document, helps a lot.
 */


#define FAT_ENTRY_DIRNAME_NO_MORE_ENTRIES ((u8)0)
#define FAT_ENTRY_DIRNAME_EMPTY_DIR ((u8)0xE5)

static u8 shortname_checksum(u8 *shortname)
{
   u8 sum = 0;

   for (int i = 0; i < 11; i++) {
      // NOTE: The operation is an unsigned char rotate right
      sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + *shortname++;
   }

   return sum;
}

/*
 * Without the NO_INLINE here, in RELEASE builds, this function gets
 * inlined in fat_walk_directory() in an apparently buggy way and it triggers
 * a triple fault.
 */
static void NO_INLINE reverse_long_name(fat_walk_dir_ctx *ctx)
{
   char tmp[256] = {0};
   int di = 0;

   for (int i = ctx->long_name_size-1; i >= 0; i--)
      tmp[di++] = ctx->long_name_buf[i];

   for (int i = 0; i < ctx->long_name_size; i++)
      ctx->long_name_buf[i] = tmp[i];
}

bool fat32_is_valid_filename_character(char c)
{
   return c >= ' ' && c <= '~' && c != '/' &&
          c != '\\' && c != '\"' && c != '*' &&
          c != ':' && c != '<' && c != '>' &&
          c != '?' && c != '|';
}

/*
 * WARNING: this implementation supports only the ASCII subset of UTF16.
 */
static void fat_handle_long_dir_entry(fat_walk_dir_ctx *ctx,
                                      fat_long_entry *le)
{
   char entrybuf[13]={0};
   int ebuf_size=0;

   if (ctx->long_name_chksum != le->LDIR_Chksum) {
      bzero(ctx->long_name_buf, sizeof(ctx->long_name_chksum));
      ctx->long_name_size = 0;
      ctx->long_name_chksum = le->LDIR_Chksum;
      ctx->is_valid = true;
   }

   if (!ctx->is_valid)
      return;

   for (int i = 0; i < 10; i+=2) {
      u8 c = le->LDIR_Name1[i];

      /* NON-ASCII characters are NOT supported */
      if (le->LDIR_Name1[i+1] != 0) {
         ctx->is_valid = false;
         return;
      }

      if (c == 0 || c == 0xFF)
         goto end;
      entrybuf[ebuf_size++] = c;
   }

   for (int i = 0; i < 12; i+=2) {
      u8 c = le->LDIR_Name2[i];

      /* NON-ASCII characters are NOT supported */
      if (le->LDIR_Name2[i+1] != 0) {
         ctx->is_valid = false;
         return;
      }

      if (c == 0 || c == 0xFF)
         goto end;
      entrybuf[ebuf_size++] = c;
   }

   for (int i = 0; i < 4; i+=2) {
      u8 c = le->LDIR_Name3[i];

      /* NON-ASCII characters are NOT supported */
      if (le->LDIR_Name3[i+1] != 0) {
         ctx->is_valid = false;
         return;
      }

      if (c == 0 || c == 0xFF)
         goto end;
      entrybuf[ebuf_size++] = c;
   }

   end:

   for (int i = ebuf_size-1; i >= 0; i--) {

      char c = entrybuf[i];

      if (!fat32_is_valid_filename_character(c)) {
         ctx->is_valid = false;
         break;
      }

      ctx->long_name_buf[ctx->long_name_size++] = c;
   }
}

int fat_walk_directory(fat_walk_dir_ctx *ctx,
                       fat_header *hdr,
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

   if (ft == fat16_type) {
      ASSERT(cluster == 0 || entry == NULL); // cluster != 0 => entry == NULL
      ASSERT(entry == NULL || cluster == 0); // entry != NULL => cluster == 0
   }

   bzero(ctx->long_name_buf, sizeof(ctx->long_name_buf));
   ctx->long_name_size = 0;
   ctx->long_name_chksum = -1;

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

         if (is_long_name_entry(&entry[i])) {
            fat_handle_long_dir_entry(ctx, (fat_long_entry*)&entry[i]);
            continue;
         }

         if (entry[i].volume_id) {
            continue; // the first "file" is the volume ID. Skip it.
         }

         // that means all the rest of the entries are free.
         if (entry[i].DIR_Name[0] == FAT_ENTRY_DIRNAME_NO_MORE_ENTRIES) {
            return 0;
         }

         // that means that the directory is empty
         if (entry[i].DIR_Name[0] == FAT_ENTRY_DIRNAME_EMPTY_DIR) {
            return 0;
         }

         /*
          * '.' is NOT a legal char in the short name. Therefore, with this
          * simple check, we skip the directories '.' and '..'.
          */
         if (entry[i].DIR_Name[0] == '.') {
            continue;
         }


         const char *long_name_ptr = NULL;

         if (ctx->long_name_size > 0 && ctx->is_valid) {

            s16 entry_checksum = shortname_checksum(entry[i].DIR_Name);
            if (ctx->long_name_chksum == entry_checksum) {
               reverse_long_name(ctx);
               long_name_ptr = (const char *) ctx->long_name_buf;
            }
         }

         int ret = cb(hdr, ft, entry + i, long_name_ptr, arg, level);

         ctx->long_name_size = 0;
         ctx->long_name_chksum = -1;

         if (ret < 0) {
            /* the callback returns a value < 0 to request a walk STOP. */
            return 0;
         }
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

   u32 FATSz;

   if (hdr->BPB_FATSz16 != 0) {
      FATSz = hdr->BPB_FATSz16;
   } else {
      fat32_header2 *h32 = (fat32_header2*) (hdr+1);
      FATSz = h32->BPB_FATSz32;
   }

   u32 FirstDataSector = hdr->BPB_RsvdSecCnt +
      (hdr->BPB_NumFATs * FATSz) + RootDirSectors;

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
   u32 i = 0;
   u32 d = 0;

   for (i = 0; i < 8 && entry->DIR_Name[i] != ' '; i++) {
      destbuf[d++] = entry->DIR_Name[i];
   }

   i = 8; // beginning of the extension part.

   if (entry->DIR_Name[i] != ' ') {
      destbuf[d++] = '.';
      for (; i < 11 && entry->DIR_Name[i] != ' '; i++) {
         destbuf[d++] = entry->DIR_Name[i];
      }
   }

   destbuf[d] = 0;
}

typedef struct {

   const char *path;          // the searched path.
   char pc[256];              // path component
   size_t pcl;                // path component's length
   char shortname[16];        // short name of the current entry
   fat_entry *result;         // the found entry
   fat_walk_dir_ctx walk_ctx; // walk context: contains long names

} fat_search_ctx;

static bool fat_fetch_next_component(fat_search_ctx *ctx)
{
   ASSERT(ctx->pcl == 0);

   /*
    * Fetch a path component from the abspath: we'll use it while iterating
    * the whole directory. On a match, we reset pcl and start a new walk on
    * the subdirectory.
    */

   while (*ctx->path && *ctx->path != '/') {
      ctx->pc[ctx->pcl++] = *ctx->path++;
   }

   ctx->pc[ctx->pcl++] = 0;
   return ctx->pcl != 0;
}

static int fat_search_entry_cb(fat_header *hdr,
                               fat_type ft,
                               fat_entry *entry,
                               const char *long_name,
                               void *arg,
                               int level)
{
   fat_search_ctx *ctx = arg;

   if (ctx->pcl == 0) {
      if (!fat_fetch_next_component(ctx)) {
         // The path was empty, so not path component has been fetched.
         return -1;
      }
   }

   /*
    * NOTE: the following is NOT fully FAT32 compliant: for long names this
    * code compares file names using a CASE SENSITIVE comparison!
    * This HACK allows a UNIX system like exOS to use FAT32 [case sensitivity
    * is a MUST in UNIX] by just forcing each file to have a long name, even
    * when that is not necessary.
    */

   if (long_name) {

      if (strcmp(long_name, ctx->pc)) {
         // no match, continue.
         return 0;
      }

      // we have a long-name match (case sensitive)

   } else {

      /*
       * no long name: for short names, we do a compliant case INSENSITVE
       * string comparison.
       */

      fat_get_short_name(entry, ctx->shortname);

      if (stricmp(ctx->shortname, ctx->pc)) {
         // no match, continue.
         return 0;
      }

      // we have a short-name match (case insensitive)
   }

   // we've found a match.

   if (*ctx->path == 0) {
      ctx->result = entry; // if the path ended, that's it. Just return.
      return -1;
   }

   /*
    * The next char in path MUST be a '/' since otherwise
    * fat_fetch_next_component() would have continued, until a '/' or a
    * '\0' is hit.
    */
   ASSERT(*ctx->path == '/');

   // path's next char is a '/': maybe there are more components in the path.
   ctx->path++;

   if (*ctx->path == 0) {
      ctx->result = entry; // the path just ended with '/'. That's still OK.
      return -1;
   }

   if (!entry->directory) {
      return -1; // if the entry is not a directory, we failed.
   }

   // The path did not end: we have to do a walk in the sub-dir.
   ctx->pcl = 0;
   fat_walk_directory(&ctx->walk_ctx, hdr,
                      ft, NULL, fat_get_first_cluster(entry),
                      &fat_search_entry_cb, ctx, level + 1);
   return -1;
}

fat_entry *
fat_search_entry(fat_header *hdr, fat_type ft, const char *abspath)
{
   if (ft == fat_unknown) {
       ft = fat_get_type(hdr);
   }

   ASSERT(*abspath == '/');
   abspath++;

   if (!*abspath) {
      /* the whole abspath was just '/', which is not a file */
      return NULL;
   }

   u32 root_dir_cluster;
   fat_entry *root = fat_get_rootdir(hdr, ft, &root_dir_cluster);

   if (*abspath == 0) {
      return root; // abspath was just '/'.
   }

   //fat_search_ctx ctx = {0};
   fat_search_ctx ctx;
   bzero(&ctx, sizeof(ctx));

   ctx.path = abspath;

   fat_walk_directory(&ctx.walk_ctx, hdr, ft, root, root_dir_cluster,
                      &fat_search_entry_cb, &ctx, 0);
   return ctx.result;
}

size_t fat_get_file_size(fat_entry *entry)
{
   return entry->DIR_FileSize;
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

      // find the next cluster
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

/////////////////////////////////////////////////

STATIC ssize_t fat_write(fs_handle h,
                         char *buf,
                         size_t bufsize)
{
   // TODO: implement
   return -1;
}


STATIC void fat_close(fs_handle handle)
{
   fat_file_handle *h = (fat_file_handle *)handle;
   kfree2(h, sizeof(fat_file_handle));
}

STATIC ssize_t fat_read(fs_handle handle,
                        char *buf,
                        size_t bufsize)
{
   fat_file_handle *h = (fat_file_handle *) handle;
   fat_fs_device_data *d = h->fs->device_data;
   u32 fsize = h->e->DIR_FileSize;
   u32 written_to_buf = 0;

   if (h->pos >= fsize) {

      /*
       * The cursor is at the end or past the end: nothing to read.
       */

      return 0;
   }

   do {

      char *data = fat_get_pointer_to_cluster_data(d->hdr, h->curr_cluster);

      const ssize_t file_rem = fsize - h->pos;
      const ssize_t buf_rem = bufsize - written_to_buf;
      const ssize_t cluster_offset = h->pos % d->cluster_size;
      const ssize_t cluster_rem = d->cluster_size - cluster_offset;
      const ssize_t to_read = MIN(cluster_rem, MIN(buf_rem, file_rem));

      memmove(buf + written_to_buf, data + cluster_offset, to_read);
      written_to_buf += to_read;
      h->pos += to_read;

      if (to_read < cluster_rem) {

         /*
          * We read less than cluster_rem because the buf was not big enough
          * or because the file was not big enough. In either case, we cannot
          * continue.
          */
         break;
      }

      // find the next cluster
      u32 fatval = fat_read_fat_entry(d->hdr, d->type, h->curr_cluster, 0);

      if (fat_is_end_of_clusterchain(d->type, fatval)) {
         ASSERT(h->pos == fsize);
         break;
      }

      // we do not expect BAD CLUSTERS
      ASSERT(!fat_is_bad_cluster(d->type, fatval));

      h->curr_cluster = fatval; // go reading the new cluster in the chain.

   } while (true);

   return written_to_buf;
}


STATIC int fat_rewind(fs_handle handle)
{
   fat_file_handle *h = (fat_file_handle *) handle;
   h->pos = 0;
   h->curr_cluster = fat_get_first_cluster(h->e);
   return 0;
}

STATIC off_t fat_seek_forward(fs_handle handle,
                              off_t dist)
{
   fat_file_handle *h = (fat_file_handle *) handle;
   fat_fs_device_data *d = h->fs->device_data;
   u32 fsize = h->e->DIR_FileSize;
   ssize_t moved_distance = 0;

   if (dist == 0)
      return h->pos;

   if (h->pos + dist > fsize) {
      /* Allow, like Linux does, to seek past the end of a file. */
      h->pos += dist;
      h->curr_cluster = (u32) -1; /* invalid cluster */
      return h->pos;
   }

   do {

      const ssize_t file_rem = fsize - h->pos;
      const ssize_t dist_rem = dist - moved_distance;
      const ssize_t cluster_offset = h->pos % d->cluster_size;
      const ssize_t cluster_rem = d->cluster_size - cluster_offset;
      const ssize_t to_move = MIN(cluster_rem, MIN(dist_rem, file_rem));

      moved_distance += to_move;
      h->pos += to_move;

      if (to_move < cluster_rem) {
         break;
      }

      // find the next cluster
      u32 fatval = fat_read_fat_entry(d->hdr, d->type, h->curr_cluster, 0);

      if (fat_is_end_of_clusterchain(d->type, fatval)) {
         ASSERT(h->pos == fsize);
         break;
      }

      // we do not expect BAD CLUSTERS
      ASSERT(!fat_is_bad_cluster(d->type, fatval));

      h->curr_cluster = fatval; // go reading the new cluster in the chain.

   } while (true);

   return h->pos;
}

STATIC off_t fat_seek(fs_handle handle,
                      off_t off,
                      int whence)
{
   off_t curr_pos = (off_t) ((fat_file_handle *)handle)->pos;

   switch (whence) {

   case SEEK_SET:

      if (off < 0)
         return -EINVAL; /* invalid negative offset */

      fat_rewind(handle);
      break;

   case SEEK_END:

      if (off >= 0)
         break;

      fat_file_handle *h = (fat_file_handle *) handle;
      off = (off_t) h->e->DIR_FileSize + off;

      if (off < 0)
         return -EINVAL;

      fat_rewind(handle);
      break;

   case SEEK_CUR:

      if (off < 0) {

         off = curr_pos + off;

         if (off < 0)
            return -EINVAL;

         fat_rewind(handle);
      }

      break;

   default:
      return -EINVAL;
   }

   return fat_seek_forward(handle, off);
}

STATIC fs_handle fat_open(filesystem *fs, const char *path)
{
   fat_fs_device_data *d = (fat_fs_device_data *) fs->device_data;

   fat_entry *e = fat_search_entry(d->hdr, d->type, path);

   if (!e) {
      return NULL; /* file not found */
   }

   fat_file_handle *h = kzmalloc(sizeof(fat_file_handle));
   VERIFY(h != NULL);

   h->fs = fs;
   h->fops.fread = fat_read;
   h->fops.fwrite = fat_write;
   h->fops.fseek = fat_seek;

   h->e = e;
   h->pos = 0;
   h->curr_cluster = fat_get_first_cluster(e);

   return h;
}

STATIC fs_handle fat_dup(fs_handle h)
{
   fat_file_handle *new_h = kzmalloc(sizeof(fat_file_handle));
   VERIFY(new_h != NULL);
   memmove(new_h, h, sizeof(fat_file_handle));
   return new_h;
}

filesystem *fat_mount_ramdisk(void *vaddr)
{
   fat_fs_device_data *d = kmalloc(sizeof(fat_fs_device_data));
   VERIFY(d != NULL);

   d->hdr = (fat_header *) vaddr;
   d->type = fat_get_type(d->hdr);
   d->cluster_size = d->hdr->BPB_SecPerClus * d->hdr->BPB_BytsPerSec;

   filesystem *fs = kzmalloc(sizeof(filesystem));
   VERIFY(fs != NULL);

   fs->device_data = d;
   fs->fopen = fat_open;
   fs->fclose = fat_close;
   fs->dup = fat_dup;

   return fs;
}

void fat_umount_ramdisk(filesystem *fs)
{
   kfree2(fs->device_data, sizeof(fat_fs_device_data));
   kfree2(fs, sizeof(filesystem));
}
