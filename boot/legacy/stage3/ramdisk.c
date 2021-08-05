/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/fat32_base.h>
#include <tilck/common/printk.h>
#include <tilck/common/color_defs.h>

#include <multiboot.h>

#include "basic_term.h"
#include "realmode_call.h"
#include "mm.h"
#include "common.h"

static void
dump_progress(const char *prefix_str, u32 curr, u32 tot)
{
   bt_movecur(bt_get_curr_row(), 0);
   printk("%s%u%% ", prefix_str, 100 * curr / tot);
}

static u32
calc_fat_ramdisk_metadata_sz(struct fat_hdr *hdr)
{
   const u32 sector_size = fat_get_sector_size(hdr);
   const u32 first_data_sector = fat_get_first_data_sector(hdr);

   /* Note: the `first_data_sector` is *relative* to the start of the ramdisk */
   return sector_size * (first_data_sector + 1);
}

static void
read_sectors_with_progress(const char *prefix_str,
                           u32 paddr, u32 first_sector, u32 count)
{
   const u32 chunk_sectors = 1024;
   const u32 chunks_count = count / chunk_sectors;
   const u32 rem = count - chunks_count * chunk_sectors;

   for (u32 chunk = 0; chunk < chunks_count; chunk++) {

      const u32 sectors_read = chunk * chunk_sectors;

      if (sectors_read > 0)
         dump_progress(prefix_str, sectors_read, count);

      read_sectors(paddr, first_sector, chunk_sectors);
      paddr += chunk_sectors * SECTOR_SIZE;
      first_sector += chunk_sectors;
   }

   if (rem > 0) {
      read_sectors(paddr, first_sector, rem);
   }

   dump_progress(prefix_str, count, count);
}

u32
rd_compact_clusters(void *ramdisk, u32 rd_size)
{
   u32 ff_clu_off;         /* offset of ramdisk's first free cluster */

   ff_clu_off = fat_get_first_free_cluster_off(ramdisk);

   if (ff_clu_off < rd_size) {

      printk("Compacting ramdisk... ");

      fat_compact_clusters(ramdisk);
      ff_clu_off = fat_get_first_free_cluster_off(ramdisk);
      rd_size = fat_calculate_used_bytes(ramdisk);

      if (rd_size != ff_clu_off) {
         printk("\n");
         panic("fat_compact_clusters() failed: %u != %u", rd_size, ff_clu_off);
      }

      write_ok_msg();
   }

   return rd_size;
}

static bool
overlap_with_kernel_file(ulong pa, ulong sz)
{
   if (!kernel_file_pa)
      return false; /* The kernel hasn't been loaded yet */

   const ulong kbegin = kernel_file_pa;
   const ulong kend = kernel_file_pa + kernel_file_sz;
   return IN_RANGE(pa, kbegin, kend) || IN_RANGE(pa + sz, kbegin, kend);
}

static bool
check_fat_header(struct fat_hdr *hdr)
{
   /*
    * Minimum sanity checks for determining if we correctly read a real FAT
    * header or some corrupted data.
    */

   if (hdr->BPB_BytsPerSec != SECTOR_SIZE)
      return false;

   switch (fat_get_type(hdr)) {

      case fat12_type:
         /* We never use FAT12: something must be wrong */
         return false;

      case fat16_type:

         {
            struct fat16_header2 *h2 = (void *)(hdr + 1);

            if (h2->BS_FilSysType[0] != 'F' ||
                h2->BS_FilSysType[1] != 'A' ||
                h2->BS_FilSysType[2] != 'T')
            {
               /*
               * The FAT specification does not require BS_FilSysType to be set,
               * but the Tilck tools and most of the FAT tools in general do set
               * this field to a reasonable value like FAT16 or FAT32. If it
               * does not start with "FAT", something is wrong.
               */
               return false;
            }
         }
         break;

      case fat32_type:

         {
            struct fat32_header2 *h2 = (void *)(hdr + 1);

            if (h2->BS_FilSysType[0] != 'F' ||
                h2->BS_FilSysType[1] != 'A' ||
                h2->BS_FilSysType[2] != 'T')
            {
               /* Same as for fat16_type */
               return false;
            }
         }

         break;

      default:

         /* We couldn't determine the FAT type */
         return false;
   }

   return true;
}

bool
load_fat_ramdisk(const char *load_str,
                 u32 first_sec,
                 ulong min_paddr,
                 ulong *ref_rd_paddr,
                 u32 *ref_rd_size,
                 bool alloc_extra_page)
{
   u32 rd_sectors;         /* rd_size in 512-bytes sectors (rounded-up) */
   u32 rd_size;            /* ramdisk size (used bytes in the fat partition) */
   u32 rd_metadata_sz;     /* size of ramdisk's metadata, including the FATs */
   ulong rd_paddr;         /* ramdisk physical address */
   ulong free_mem;
   ulong size_to_alloc;

   printk("%s", load_str);
   free_mem = get_usable_mem(&g_meminfo, min_paddr, SECTOR_SIZE);

   if (!free_mem || overlap_with_kernel_file(free_mem, SECTOR_SIZE))
      goto oom;

   // Read FAT's header
   read_sectors(free_mem, first_sec, 1 /* read just 1 sector */);

   // Do some sanity checks against data corruption
   if (!check_fat_header((void *)free_mem))
      goto corrupted;

   // Determine FAT's metadata size
   rd_metadata_sz = calc_fat_ramdisk_metadata_sz((void *)free_mem);

   // Get a free mem area big enough for it
   free_mem = get_usable_mem(&g_meminfo, min_paddr, rd_metadata_sz);

   if (!free_mem || overlap_with_kernel_file(free_mem, rd_metadata_sz))
      goto oom;

   // Now read all the meta-data up to the first data sector.
   read_sectors(free_mem, first_sec, rd_metadata_sz / SECTOR_SIZE);

   // Finally we're able to determine how big is the fatpart (pure data)
   rd_size = fat_calculate_used_bytes((void *)free_mem);

   /* Calculate rd_size in sectors, rounding up at SECTOR_SIZE */
   rd_sectors = (rd_size + SECTOR_SIZE - 1) / SECTOR_SIZE;

   size_to_alloc = SECTOR_SIZE * rd_sectors;

   if (alloc_extra_page)
      size_to_alloc += PAGE_SIZE;

   // Finally, get a mem area big enough for the whole FAT partition
   free_mem = get_usable_mem(&g_meminfo, min_paddr, size_to_alloc);

   if (!free_mem || overlap_with_kernel_file(free_mem, size_to_alloc))
      goto oom;

   rd_paddr = free_mem;
   read_sectors_with_progress(load_str,
                              rd_paddr,
                              first_sec,
                              rd_sectors);

   bt_movecur(bt_get_curr_row(), 0);
   printk("%s", load_str);
   write_ok_msg();

   /* Return ramdisk's paddr and size using the OUT parameters */
   *ref_rd_paddr = rd_paddr;
   *ref_rd_size = rd_size;
   return true;

oom:
   printk("No free memory for loading the ramdisk\n");
   goto end;

corrupted:
   printk("FAT header corrupted\n");
   goto end;

end:
   write_fail_msg();
   return false;
}
