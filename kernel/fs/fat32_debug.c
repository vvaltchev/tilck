/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/fs/fat32.h>

/*
 * *********************************************
 * DEBUG util code
 * *********************************************
 */

static void dump_fixed_str(const char *what, char *str, u32 len)
{
   char buf[256];
   len = MIN((u32)ARRAY_SIZE(buf), len);

   buf[len]=0;
   memcpy(buf, str, len);
   printk("%s: '%s'\n", what, buf);
}

void fat_dump_common_header(void *data)
{
   struct fat_hdr *bpb = data;

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


static void dump_fat16_headers(struct fat_hdr *common_hdr)
{
   struct fat16_header2 *hdr = (struct fat16_header2*) (common_hdr+1);

   printk("BS_DrvNum: %u\n", hdr->BS_DrvNum);
   printk("BS_BootSig: %u\n", hdr->BS_BootSig);
   printk("BS_VolID: %p\n", hdr->BS_VolID);
   dump_fixed_str("BS_VolLab", hdr->BS_VolLab, sizeof(hdr->BS_VolLab));
   dump_fixed_str("BS_FilSysType",
                  hdr->BS_FilSysType, sizeof(hdr->BS_FilSysType));
}

static void dump_fat32_headers(struct fat_hdr *common_hdr)
{
   struct fat32_header2 *hdr = (struct fat32_header2*) (common_hdr+1);
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

static void dump_entry_attrs(struct fat_entry *entry)
{
   printk("readonly:  %u\n", entry->readonly);
   printk("hidden:    %u\n", entry->hidden);
   printk("system:    %u\n", entry->system);
   printk("vol id:    %u\n", entry->volume_id);
   printk("directory: %u\n", entry->directory);
   printk("archive:   %u\n", entry->archive);
}

struct debug_fat_walk_ctx {

   struct fat_walk_static_params walk_params;
   struct fat_walk_long_name_ctx walk_ctx;
   int level;
};

static int dump_dir_entry(struct fat_hdr *hdr,
                          enum fat_type ft,
                          struct fat_entry *entry,
                          const char *long_name,
                          void *arg)
{
   char shortname[16];
   fat_get_short_name(entry, shortname);
   struct debug_fat_walk_ctx *ctx = arg;

   char indentbuf[4*16] = {0};
   for (int i = 0; i < 4 * ctx->level; i++)
      indentbuf[i] = ' ';

   if (!entry->directory) {
      printk("%s%s: %u bytes\n",
             indentbuf,
             long_name ? long_name : shortname,
             entry->DIR_FileSize);
   } else {
      printk("%s%s\n",
             indentbuf,
             long_name ? long_name : shortname);
   }

   if (entry->directory) {
      if (strcmp(shortname, ".") && strcmp(shortname, "..")) {

         ctx->level++;

         fat_walk_directory(&ctx->walk_params,
                            NULL,
                            fat_get_first_cluster(entry));

         ctx->level--;
      }
   }

   return 0;
}

void fat_dump_info(void *fatpart_begin)
{
   struct fat_hdr *hdr = fatpart_begin;
   fat_dump_common_header(fatpart_begin);

   printk("\n");

   enum fat_type ft = fat_get_type(hdr);
   ASSERT(ft != fat12_type);

   if (ft == fat16_type) {
      dump_fat16_headers(fatpart_begin);
   } else {
      dump_fat32_headers(hdr);
   }
   printk("\n");

   u32 root_dir_cluster;
   struct fat_entry *root = fat_get_rootdir(hdr, ft, &root_dir_cluster);

   struct debug_fat_walk_ctx ctx = {

      .walk_params = {
         .ctx = &ctx.walk_ctx,
         .h = hdr,
         .ft = ft,
         .cb = &dump_dir_entry,
         .arg = &ctx,
      },
      .level = 0,
   };

   fat_walk_directory(&ctx.walk_params, root, root_dir_cluster);
}
