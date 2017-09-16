
#include <common_defs.h>
#include <string_util.h>

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

} fat_bpb;

static void dump_fixed_str(const char *what, char *str, u32 len)
{
   char buf[len+1];
   buf[len]=0;
   memcpy(buf, str, len);
   printk("%s: %s\n", what, buf);
}

void fat32_dump_common_header(void *data)
{
   fat_bpb *bpb = data;

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
   s8 BS_FilSysType[11];

} fat16_bpb;

void fat32_dump_info(void *fatpart_begin)
{
   fat32_dump_common_header(fatpart_begin);



/*
   fat_file *hdr = fatpart_begin;

   if (hdr->short_filename[0] != 0xE5)
      printk("short filename: %s\n", hdr->short_filename);

   printk("file size: %u\n", hdr->filesize);
   printk("first clust: %u\n", hdr->first_clust_hi << 16 | hdr->first_clust_lo);
*/
}


typedef struct __attribute__(( packed )) {

   u8 short_filename[11];

   u8 unused1 : 1;
   u8 unused2 : 1;
   u8 archive : 1;
   u8 directory : 1;
   u8 volume_id : 1;
   u8 system : 1;
   u8 hidden : 1;
   u8 readonly : 1;

   u16 first_clust_hi;
   u16 first_clust_lo;
   u32 filesize;

} fat_file;
