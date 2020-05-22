/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/failsafe_assert.h>
#include <tilck/common/string_util.h>
#include <tilck/common/fat32_base.h>


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


#define FAT_ENTRY_LAST                       ((char)0)
#define FAT_ENTRY_AVAILABLE                  ((char)0xE5)

static u8 shortname_checksum(u8 *shortname)
{
   u8 sum = 0;

   for (int i = 0; i < 11; i++) {
      // NOTE: The operation is an unsigned char rotate right
      sum = (u8)( ((sum & 1u) ? 0x80u : 0u) + (sum >> 1u) + *shortname++ );
   }

   return sum;
}

static const bool fat32_valid_chars[256] =
{
   [0 ... 32] = 0,

   [33] = 0, /* ! */
   [34] = 0, /* " */
   [35] = 1, /* # */
   [36] = 1, /* $ */
   [37] = 1, /* % */
   [38] = 1, /* & */
   [39] = 1, /* ' */
   [40] = 1, /* ( */
   [41] = 1, /* ) */
   [42] = 0, /* * */
   [43] = 1, /* + */
   [44] = 1, /* , */
   [45] = 1, /* - */
   [46] = 1, /* . */
   [47] = 0, /* / */
   [48] = 1, /* 0 */
   [49] = 1, /* 1 */
   [50] = 1, /* 2 */
   [51] = 1, /* 3 */
   [52] = 1, /* 4 */
   [53] = 1, /* 5 */
   [54] = 1, /* 6 */
   [55] = 1, /* 7 */
   [56] = 1, /* 8 */
   [57] = 1, /* 9 */
   [58] = 0, /* : */
   [59] = 1, /* ; */
   [60] = 0, /* < */
   [61] = 1, /* = */
   [62] = 0, /* > */
   [63] = 0, /* ? */
   [64] = 1, /* @ */
   [65] = 1, /* A */
   [66] = 1, /* B */
   [67] = 1, /* C */
   [68] = 1, /* D */
   [69] = 1, /* E */
   [70] = 1, /* F */
   [71] = 1, /* G */
   [72] = 1, /* H */
   [73] = 1, /* I */
   [74] = 1, /* J */
   [75] = 1, /* K */
   [76] = 1, /* L */
   [77] = 1, /* M */
   [78] = 1, /* N */
   [79] = 1, /* O */
   [80] = 1, /* P */
   [81] = 1, /* Q */
   [82] = 1, /* R */
   [83] = 1, /* S */
   [84] = 1, /* T */
   [85] = 1, /* U */
   [86] = 1, /* V */
   [87] = 1, /* W */
   [88] = 1, /* X */
   [89] = 1, /* Y */
   [90] = 1, /* Z */
   [91] = 1, /* [ */
   [92] = 0, /* \ */
   [93] = 1, /* ] */
   [94] = 1, /* ^ */
   [95] = 1, /* _ */
   [96] = 1, /* ` */
   [97] = 1, /* a */
   [98] = 1, /* b */
   [99] = 1, /* c */
   [100] = 1, /* d */
   [101] = 1, /* e */
   [102] = 1, /* f */
   [103] = 1, /* g */
   [104] = 1, /* h */
   [105] = 1, /* i */
   [106] = 1, /* j */
   [107] = 1, /* k */
   [108] = 1, /* l */
   [109] = 1, /* m */
   [110] = 1, /* n */
   [111] = 1, /* o */
   [112] = 1, /* p */
   [113] = 1, /* q */
   [114] = 1, /* r */
   [115] = 1, /* s */
   [116] = 1, /* t */
   [117] = 1, /* u */
   [118] = 1, /* v */
   [119] = 1, /* w */
   [120] = 1, /* x */
   [121] = 1, /* y */
   [122] = 1, /* z */
   [123] = 1, /* { */
   [124] = 0, /* | */
   [125] = 1, /* } */
   [126] = 1, /* ~ */

   [127 ... 255] = 0,
};

bool fat32_is_valid_filename_character(char c)
{
   return fat32_valid_chars[(u8)c];
}

/*
 * WARNING: this implementation supports only the ASCII subset of UTF16.
 */
static void fat_handle_long_dir_entry(struct fat_walk_long_name_ctx *ctx,
                                      struct fat_long_entry *le)
{
   char entrybuf[13] = {0};
   int ebuf_size=0;

   if (ctx->lname_chksum != le->LDIR_Chksum) {
      bzero(ctx->lname_buf, sizeof(ctx->lname_chksum));
      ctx->lname_sz = 0;
      ctx->lname_chksum = le->LDIR_Chksum;
      ctx->is_valid = true;
   }

   if (!ctx->is_valid)
      return;

   for (int i = 0; i < 10; i += 2) {

      u8 c = le->LDIR_Name1[i];

      /* NON-ASCII characters are NOT supported */
      if (le->LDIR_Name1[i+1] != 0) {
         ctx->is_valid = false;
         return;
      }

      if (c == 0 || c == 0xFF)
         goto end;

      entrybuf[ebuf_size++] = (char)c;
   }

   for (int i = 0; i < 12; i += 2) {

      u8 c = le->LDIR_Name2[i];

      /* NON-ASCII characters are NOT supported */
      if (le->LDIR_Name2[i+1] != 0) {
         ctx->is_valid = false;
         return;
      }

      if (c == 0 || c == 0xFF)
         goto end;

      entrybuf[ebuf_size++] = (char)c;
   }

   for (int i = 0; i < 4; i += 2) {

      u8 c = le->LDIR_Name3[i];

      /* NON-ASCII characters are NOT supported */
      if (le->LDIR_Name3[i+1] != 0) {
         ctx->is_valid = false;
         return;
      }

      if (c == 0 || c == 0xFF)
         goto end;

      entrybuf[ebuf_size++] = (char)c;
   }

   end:

   for (int i = ebuf_size-1; i >= 0; i--) {

      char c = entrybuf[i];

      if (!fat32_is_valid_filename_character(c)) {
         ctx->is_valid = false;
         break;
      }

      ctx->lname_buf[ctx->lname_sz++] = (u8) c;
   }
}

static const char *
finalize_long_name(struct fat_walk_long_name_ctx *ctx,
                   struct fat_entry *e)
{
   const s16 e_checksum = shortname_checksum((u8 *)e->DIR_Name);

   if (ctx->lname_chksum == e_checksum) {
      ctx->lname_buf[ctx->lname_sz] = 0;
      str_reverse((char *)ctx->lname_buf, (size_t)ctx->lname_sz);
      return (const char *) ctx->lname_buf;
   }

   return NULL;
}

int
fat_walk(struct fat_walk_static_params *p, u32 cluster)
{
   struct fat_walk_long_name_ctx *const ctx = p->ctx;
   const u32 entries_per_cluster = fat_get_dir_entries_per_cluster(p->h);
   struct fat_entry *dentries = NULL;

   ASSERT(p->ft == fat16_type || p->ft == fat32_type);

   if (cluster == 0)
      dentries = fat_get_rootdir(p->h, p->ft, &cluster);

   if (ctx) {
      bzero(ctx->lname_buf, sizeof(ctx->lname_buf));
      ctx->lname_sz = 0;
      ctx->lname_chksum = -1;
      ctx->is_valid = false;
   }

   while (true) {

      if (cluster != 0) {

         /*
          * if cluster != 0, cluster is used and entry is overriden.
          * That's because on FAT16 we know only the sector of the root dir.
          * In that case, fat_get_rootdir() returns 0 as cluster. In all the
          * other cases, we need only the cluster.
          */
         dentries = fat_get_pointer_to_cluster_data(p->h, cluster);
      }

      ASSERT(dentries != NULL);

      for (u32 i = 0; i < entries_per_cluster; i++) {

         if (ctx && is_long_name_entry(&dentries[i])) {
            fat_handle_long_dir_entry(ctx, (void *)&dentries[i]);
            continue;
         }

         // the first "file" is the volume ID. Skip it.
         if (dentries[i].volume_id)
            continue;

         // the entry was used, but now is free
         if (dentries[i].DIR_Name[0] == FAT_ENTRY_AVAILABLE)
            continue;

         // that means all the rest of the entries are free.
         if (dentries[i].DIR_Name[0] == FAT_ENTRY_LAST)
            return 0;

         const char *long_name_ptr = NULL;

         if (ctx && ctx->lname_sz > 0 && ctx->is_valid)
            long_name_ptr = finalize_long_name(ctx, &dentries[i]);

         int ret = p->cb(p->h, p->ft, dentries + i, long_name_ptr, p->arg);

         if (ctx) {
            ctx->lname_sz = 0;
            ctx->lname_chksum = -1;
         }

         if (ret) {
            /* the callback returns a value != 0 to request a walk STOP. */
            return 0;
         }
      }

      /*
       * In case fat_walk has been called on the root dir on a FAT16,
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
      u32 val = fat_read_fat_entry(p->h, p->ft, 0, cluster);

      if (fat_is_end_of_clusterchain(p->ft, val))
         break; // that's it: we hit an exactly full cluster.

      /* We do not handle BAD CLUSTERS */
      ASSERT(!fat_is_bad_cluster(p->ft, val));

      cluster = val;
   }

   return 0;
}

u32 fat_get_cluster_count(struct fat_hdr *hdr)
{
   const u32 FATSz = fat_get_FATSz(hdr);
   const u32 TotSec = fat_get_TotSec(hdr);
   const u32 RootDirSectors = fat_get_RootDirSectors(hdr);
   const u32 FatAreaSize = hdr->BPB_NumFATs * FATSz;
   const u32 DataSec = TotSec-(hdr->BPB_RsvdSecCnt+FatAreaSize+RootDirSectors);
   return DataSec / hdr->BPB_SecPerClus;
}

enum fat_type fat_get_type(struct fat_hdr *hdr)
{
   const u32 CountofClusters = fat_get_cluster_count(hdr);

   if (CountofClusters < 4085) {

      /* Volume is FAT12 */
      return fat12_type;

   } else if (CountofClusters < 65525) {

      /* Volume is FAT16 */
      return fat16_type;

   } else {

      /* Volume is FAT32 */
      return fat32_type;
   }
}

static void *
fat_get_entry_ptr(struct fat_hdr *h, enum fat_type ft, u32 fatN, u32 clu)
{
   STATIC_ASSERT(fat16_type == 2);
   STATIC_ASSERT(fat32_type == 4);

   ASSERT(ft == fat16_type || ft == fat32_type);
   ASSERT(fatN < h->BPB_NumFATs);

   const u32 FATSz = fat_get_FATSz(h);
   const u32 FATOffset = clu * ft;

   const u32 ThisFATSecNum =
      fatN * FATSz + h->BPB_RsvdSecCnt + (FATOffset / h->BPB_BytsPerSec);

   const u32 ThisFATEntOffset = FATOffset % h->BPB_BytsPerSec;
   u8 *const SecBuf = (u8*)h + ThisFATSecNum * h->BPB_BytsPerSec;

   return SecBuf + ThisFATEntOffset;
}

/*
 * Reads the entry in the FAT 'fatN' for cluster 'clusterN'.
 * The entry may be 16 or 32 bit. It returns 32-bit integer for convenience.
 */
u32
fat_read_fat_entry(struct fat_hdr *h, enum fat_type ft, u32 fatN, u32 clusterN)
{
   void *ptr;
   ASSERT(ft == fat16_type || ft == fat32_type);

   ptr = fat_get_entry_ptr(h, ft, fatN, clusterN);
   return ft == fat16_type ? *(u16 *)ptr : (*(u32 *)ptr) & 0x0FFFFFFF;
}

void
fat_write_fat_entry(struct fat_hdr *h,
                    enum fat_type ft,
                    u32 fatN,
                    u32 clusterN,
                    u32 value)
{
   void *ptr;
   ASSERT(ft == fat16_type || ft == fat32_type);

   ptr = fat_get_entry_ptr(h, ft, fatN, clusterN);

   if (ft == fat16_type) {

      ASSERT((value & 0xFFFF0000U) == 0); /* the top 16 bits cannot be used */
      *(u16 *)ptr = (u16)value;

   } else {

      ASSERT((value & 0xF0000000U) == 0); /* the top 4 bits cannot be used */
      u32 oldval = *(u32 *)ptr & 0xF0000000U;
      *(u32 *)ptr = oldval | value;
   }
}

u32 fat_get_first_data_sector(struct fat_hdr *hdr)
{
   u32 RootDirSectors = fat_get_RootDirSectors(hdr);
   u32 FATSz;

   if (hdr->BPB_FATSz16 != 0) {
      FATSz = hdr->BPB_FATSz16;
   } else {
      struct fat32_header2 *h32 = (struct fat32_header2*) (hdr+1);
      FATSz = h32->BPB_FATSz32;
   }

   u32 FirstDataSector = hdr->BPB_RsvdSecCnt +
      (hdr->BPB_NumFATs * FATSz) + RootDirSectors;

   return FirstDataSector;
}

u32 fat_get_sector_for_cluster(struct fat_hdr *hdr, u32 N)
{
   u32 FirstDataSector = fat_get_first_data_sector(hdr);

   // FirstSectorofCluster
   return ((N - 2) * hdr->BPB_SecPerClus) + FirstDataSector;
}

struct fat_entry *
fat_get_rootdir(struct fat_hdr *hdr, enum fat_type ft, u32 *cluster /* out */)
{
   ASSERT(ft == fat16_type || ft == fat32_type);
   u32 sector;

   if (ft == fat16_type) {

      u32 FirstDataSector =
         (u32)hdr->BPB_RsvdSecCnt + (u32)(hdr->BPB_NumFATs * hdr->BPB_FATSz16);

      sector = FirstDataSector;
      *cluster = 0; /* On FAT16 the root dir entry is NOT a cluster chain! */

   } else {

      /* FAT32 case */
      struct fat32_header2 *h32 = (struct fat32_header2 *) (hdr + 1);
      *cluster = h32->BPB_RootClus;
      sector = fat_get_sector_for_cluster(hdr, *cluster);
   }

   return (struct fat_entry*) ((u8*)hdr + (hdr->BPB_BytsPerSec * sector));
}

void fat_get_short_name(struct fat_entry *entry, char *destbuf)
{
   u32 i = 0;
   u32 d = 0;

   for (i = 0; i < 8 && entry->DIR_Name[i] != ' '; i++) {

      char c = entry->DIR_Name[i];

      destbuf[d++] =
         (entry->DIR_NTRes & FAT_ENTRY_NTRES_BASE_LOW_CASE)
            ? (char)tolower(c)
            : c;
   }

   i = 8; // beginning of the extension part.

   if (entry->DIR_Name[i] != ' ') {

      destbuf[d++] = '.';

      for (; i < 11 && entry->DIR_Name[i] != ' '; i++) {

         char c = entry->DIR_Name[i];

         destbuf[d++] =
            (entry->DIR_NTRes & FAT_ENTRY_NTRES_EXT_LOW_CASE)
               ? (char)tolower(c)
               : c;
      }
   }

   destbuf[d] = 0;
}

static bool fat_fetch_next_component(struct fat_search_ctx *ctx)
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

int fat_search_entry_cb(struct fat_hdr *hdr,
                        enum fat_type ft,
                        struct fat_entry *entry,
                        const char *long_name,
                        void *arg)
{
   struct fat_search_ctx *ctx = arg;

   if (ctx->pcl == 0) {
      if (!fat_fetch_next_component(ctx)) {
         // The path was empty, so no path component has been fetched.
         return -1;
      }
   }

   /*
    * NOTE: the following is NOT fully FAT32 compliant: for long names this
    * code compares file names using a CASE SENSITIVE comparison!
    * This HACK allows a UNIX system like Tilck to use FAT32 [case sensitivity
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
       * no long name: for short names, we do a compliant case INSENSITIVE
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

   if (ctx->single_comp || *ctx->path == 0) {
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

      /*
       * The path just ended with '/'. That's OK only if entry is acutally is
       * a directory.
       */

      if (entry->directory)
         ctx->result = entry;
      else
         ctx->not_dir = true;

      return -1;
   }

   if (!entry->directory)
      return -1; // if the entry is not a directory, we failed.

   // The path did not end: we have to do a walk in the sub-dir.
   ctx->pcl = 0;
   ctx->subdir_cluster = fat_get_first_cluster(entry);
   return -1;
}

void
fat_init_search_ctx(struct fat_search_ctx *ctx,
                    const char *path,
                    bool single_comp)
{
   bzero(ctx, sizeof(struct fat_search_ctx));

#ifdef __clang_analyzer__
   ctx->pcl = 0;       /* SA: make it sure ctx.pcl is zeroed */
   ctx->result = NULL; /* SA: make it sure ctx.result is zeroed */
#endif

   ctx->path = path;
   ctx->single_comp = single_comp;
}

struct fat_entry *
fat_search_entry(struct fat_hdr *hdr,
                 enum fat_type ft,
                 const char *abspath,
                 int *err)
{
   struct fat_walk_static_params walk_params;
   struct fat_search_ctx ctx;

   if (ft == fat_unknown)
       ft = fat_get_type(hdr);

   ASSERT(*abspath == '/');
   abspath++;

   if (!*abspath) {
      /* the whole abspath was just '/' */
      u32 unused;
      return fat_get_rootdir(hdr, ft, &unused);
   }

   walk_params = (struct fat_walk_static_params) {
      .ctx = &ctx.walk_ctx,
      .h = hdr,
      .ft = ft,
      .cb = &fat_search_entry_cb,
      .arg = &ctx,
   };

   fat_init_search_ctx(&ctx, abspath, false);
   fat_walk(&walk_params, 0);

   while (ctx.subdir_cluster) {

      const u32 cluster = ctx.subdir_cluster;
      ctx.subdir_cluster = 0;
      fat_walk(&walk_params, cluster);
   }

   if (err) {
      if (ctx.not_dir)
         *err = -20; /* -ENOTDIR */
      else
         *err = !ctx.result ? -2 /* ENOENT */: 0;
   }

   return ctx.result;
}

size_t fat_get_file_size(struct fat_entry *entry)
{
   return entry->DIR_FileSize;
}

void
fat_read_whole_file(struct fat_hdr *hdr,
                    struct fat_entry *entry,
                    char *dest_buf,
                    size_t dest_buf_size)
{
   ASSERT(entry->DIR_FileSize <= dest_buf_size);

   const enum fat_type ft = fat_get_type(hdr);
   const u32 cs = fat_get_cluster_size(hdr);

   size_t written = 0;
   size_t fsize = entry->DIR_FileSize;
   u32 cluster = fat_get_first_cluster(entry);

   do {

      char *data = fat_get_pointer_to_cluster_data(hdr, cluster);

      size_t rem = fsize - written;

      if (rem <= cs) {
         // read what is needed
         memcpy(dest_buf + written, data, rem);
         /* written += rem; */ // Avoid SA warning "dead increment"
         break;
      }

      // read the whole cluster
      memcpy(dest_buf + written, data, cs);
      written += cs;

      ASSERT((fsize - written) > 0);

      // find the next cluster
      u32 fatval = fat_read_fat_entry(hdr, ft, 0, cluster);

      if (fat_is_end_of_clusterchain(ft, fatval)) {
         // rem is still > 0, this should NOT be the last cluster
         NOT_REACHED();
      }

      // we do not expect BAD CLUSTERS
      ASSERT(!fat_is_bad_cluster(ft, fatval));

      cluster = fatval; // go reading the new cluster in the chain.

   } while (written < fsize);
}

struct compact_ctx {

   struct fat_walk_static_params walk_params;
   u32 ffc;                                     /* first free cluster */
};

static int
fat_compact_walk_cb(struct fat_hdr *hdr,
                    enum fat_type ft,
                    struct fat_entry *e,
                    const char *longname,
                    void *arg)
{
   const u32 first_clu = fat_get_first_cluster(e);
   const u32 clu_size = fat_get_cluster_size(hdr);
   u32 clu = first_clu, next_clu, last_clu = 0;
   struct compact_ctx *const ctx = arg;

   {
      char name[16];
      fat_get_short_name(e, name);

      if (is_dot_or_dotdot(name, (int)strlen(name)))
         return 0; /* It makes no sense to visit '.' and '..' */
   }

   do {

      next_clu = fat_read_fat_entry(hdr, ft, 0, clu);

      /* Move forward `ctx->ffc`, if necessary */
      while (fat_read_fat_entry(hdr, ft, 0, ctx->ffc)) {
         ctx->ffc++;
      }

      if (clu > ctx->ffc) {

         void *src = fat_get_pointer_to_cluster_data(hdr, clu);
         void *dest = fat_get_pointer_to_cluster_data(hdr, ctx->ffc);
         memcpy(dest, src, clu_size);

         if (clu == first_clu)
            fat_set_first_cluster(e, ctx->ffc);
         else
            fat_write_fat_entry(hdr, ft, 0, last_clu, ctx->ffc);

         fat_write_fat_entry(hdr, ft, 0, ctx->ffc, next_clu);
         fat_write_fat_entry(hdr, ft, 0, clu, 0);
         clu = ctx->ffc++;
      }

      last_clu = clu;
      clu = next_clu;

   } while (!fat_is_end_of_clusterchain(ft, clu));


   if (e->directory) {
      fat_walk(&ctx->walk_params, fat_get_first_cluster(e));
   }

   return 0;
}

void fat_compact_clusters(struct fat_hdr *hdr)
{
   const u32 count = fat_get_cluster_count(hdr);
   const enum fat_type ft = fat_get_type(hdr);
   struct compact_ctx cctx;

   for (cctx.ffc = 0; cctx.ffc < count; cctx.ffc++) {
      if (!fat_read_fat_entry(hdr, ft, 0, cctx.ffc))
         break;
   }

   cctx.walk_params = (struct fat_walk_static_params) {
      .ctx = NULL,
      .h = hdr,
      .ft = ft,
      .cb = &fat_compact_walk_cb,
      .arg = &cctx,
   };

   fat_walk(&cctx.walk_params, 0);
}

u32
fat_get_first_free_cluster_off(struct fat_hdr *hdr)
{
   u32 clu, ff_sector;
   const u32 cluster_count = fat_get_cluster_count(hdr);
   const enum fat_type ft = fat_get_type(hdr);

   for (clu = 0; clu < cluster_count; clu++) {
      if (!fat_read_fat_entry(hdr, ft, 0, clu))
         break;
   }

   ff_sector = fat_get_sector_for_cluster(hdr, clu);
   return (ff_sector + 1) * hdr->BPB_BytsPerSec;
}

u32
fat_calculate_used_bytes(struct fat_hdr *hdr)
{
   const u32 cluster_count = fat_get_cluster_count(hdr);
   const enum fat_type ft = fat_get_type(hdr);
   u32 val, clu, ff_sector;

   for (clu = cluster_count; clu > 0; clu--) {

      val = fat_read_fat_entry(hdr, ft, 0, clu-1);

      if (val && !fat_is_bad_cluster(ft, val))
         break;
   }

   if (!clu)
      return 0; /* fat partition completely corrupt */

   ff_sector = fat_get_sector_for_cluster(hdr, clu);
   return (ff_sector + 1) * hdr->BPB_BytsPerSec;
}
