/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#define BPB_OFFSET                  0x00b
#define PART_TABLE_OFFSET           0x1be
#define DISK_UUID_OFFSET            0x1b8
#define MAX_EBPB_SIZE                 128

#define INFO(fmt, ...)                                       \
   do {                                                      \
      if (!opt_quiet)                                        \
         fprintf(stdout, "INFO: " fmt, ##__VA_ARGS__);       \
   } while (0)

#define WARNING(fmt, ...)     fprintf(stderr, "WARNING: " fmt, ##__VA_ARGS__)
#define ERROR(fmt, ...)       fprintf(stderr, "ERROR: " fmt, ##__VA_ARGS__)

/* DOS 3.31 BPB */
struct PACKED bpb {

   u16 sector_size;
   u8 sec_per_cluster;
   u16 res_sectors;
   u8 num_fats;
   u16 root_entries;
   u16 small_sector_count;
   u8 media_type;

   u16 sectors_per_fat;

   u16 sectors_per_track;
   u16 heads_per_cyl;
   u32 hidden_sectors;
   u32 large_sector_count;

   char __extra[0];        /* declare we're going to write out-of-bounds */
};

struct PACKED ebpb34 {

   struct bpb bpb;

   u8 drive_num;
   u8 bflags;
   u8 boot_sig;
   u32 serial;
};

struct PACKED mbr_part {

   u8 boot;
   u8 start_head;
   u8 start_sec : 6;
   u8 start_hi_cyl : 2;
   u8 start_cyl;

   u8 id;
   u8 end_head;
   u8 end_sec : 6;
   u8 end_hi_cyl : 2;
   u8 end_cyl;

   u32 lba_start;
   u32 lba_tot;
};

static inline u32 bpb_get_sectors_count(struct bpb *b)
{
   return b->small_sector_count
            ? b->small_sector_count
            : b->large_sector_count;
}

static inline u16 get_start_cyl(struct mbr_part *p)
{
   return (u16)p->start_cyl | (((u16)p->start_hi_cyl) << 8);
}

static inline u16 get_end_cyl(struct mbr_part *p)
{
   return (u16)p->end_cyl | (((u16)p->end_hi_cyl) << 8);
}

static inline void set_start_cyl(struct mbr_part *p, u16 val)
{
   p->start_cyl = val & 0xff;
   p->start_hi_cyl = (val >> 8) & 3;
}

static inline void set_end_cyl(struct mbr_part *p, u16 val)
{
   p->end_cyl = val & 0xff;
   p->end_hi_cyl = (val >> 8) & 3;
}

static u32 chs_to_lba(struct bpb *b, u32 c, u32 h, u32 s)
{
   return (c * b->heads_per_cyl + h) * b->sectors_per_track + (s - 1);
}

static void lba_to_chs(struct bpb *b, u32 lba, u16 *c, u16 *h, u16 *s)
{
   *c = lba / (b->heads_per_cyl * b->sectors_per_track);
   *h = (lba / b->sectors_per_track) % b->heads_per_cyl;
   *s = (lba % b->sectors_per_track) + 1;
}

static u32 get_part_start(struct bpb *b, struct mbr_part *p)
{
   return chs_to_lba(b, get_start_cyl(p), p->start_head, p->start_sec);
}

static u32 get_part_end(struct bpb *b, struct mbr_part *p)
{
   return chs_to_lba(b, get_end_cyl(p), p->end_head, p->end_sec);
}

struct PACKED mbr_part_table {
   struct mbr_part partitions[4];
};

struct mbr_info {
   unsigned char jmp[3];     /* [0x0000, 0x0003)                              */
   char *raw_buf;            /* [0x0000, 0x0200) : Contains the whole MBR     */
   struct bpb *b;            /* [0x000B: variable length]                     */
   u32 disk_uuid;            /* [0x01B8, 0x01BC)                              */
   struct mbr_part_table *t; /* [0x01BE, 0x01FE)                              */
   bool bpb_checked;
   bool bpb_broken;
};


/*
 * Generic bootable MBR:
 *
 * It scans the partition table looking for an *active* partition, loads
 * 1 sector from it and gets out of the way so that boot can continue from
 * that partition.
 *
 * Technically, we don't really need this:
 *
 *    1. For legacy boot, we have our own custom bootloader and we won't use
 *       this one.
 *
 *    2. For UEFI boot, code is never run from the MBR.
 *
 * However, a wide variety of tools won't consider an MBR valid if it's not
 * bootable. Since we're using MBR even for the pure UEFI boot, we have to
 * make the MBR look bootable, just for the sake of various tools like Rufus
 * etc. The UEFI firmware will just ignore this code.
 */

static const unsigned char mbr[] = {
   0xfa, 0xb8, 0x00, 0x10, 0x8e, 0xd0, 0xbc, 0x00, 0xb0, 0xb8, 0x00, 0x00,
   0x8e, 0xd8, 0x8e, 0xc0, 0xfb, 0xbe, 0x00, 0x7c, 0xbf, 0x00, 0x06, 0xb9,
   0x00, 0x02, 0xf3, 0xa4, 0xea, 0x21, 0x06, 0x00, 0x00, 0xbe, 0xbe, 0x07,
   0x38, 0x04, 0x75, 0x0b, 0x83, 0xc6, 0x10, 0x81, 0xfe, 0xfe, 0x07, 0x75,
   0xf3, 0xeb, 0x16, 0xb4, 0x02, 0xb0, 0x01, 0xbb, 0x00, 0x7c, 0xb2, 0x80,
   0x8a, 0x74, 0x01, 0x8b, 0x4c, 0x02, 0xcd, 0x13, 0xea, 0x00, 0x7c, 0x00,
   0x00, 0xeb, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

   0x00, 0x00, 0x00, 0x00, /* Disk UUID */

   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

   0x55, 0xaa /* Bootable signature */
};

STATIC_ASSERT(sizeof(mbr) == 512);

static void dump_bpb(struct bpb *b)
{
   printf("\n");
   printf("Bios Parameter Block (DOS 3.31):\n");
   printf("    Media type:       %#9x\n", b->media_type);
   printf("    Sector size:       %8u\n", b->sector_size);
   printf("    Heads per cyl:     %8u\n", b->heads_per_cyl);
   printf("    Sectors per track: %8u\n", b->sectors_per_track);
   printf("    Reserved sectors:  %8u\n", b->res_sectors);
   printf("    Hidden sectors:    %8u\n", b->hidden_sectors);
   printf("    Tot sectors count: %8u\n", bpb_get_sectors_count(b));
   printf("\n");

   struct ebpb34 *ext = (void *)b;

   if (ext->boot_sig == 0x28 || ext->boot_sig == 0x29) {
      printf("Extended fields (DOS 3.4 EBPB):\n");
      printf("    BPB signature:   %#10x\n", ext->boot_sig);
      printf("    Serial num:      %#10x\n", ext->serial);
   }
}

static int bpb_check(struct mbr_info *nfo)
{
   struct bpb *b = nfo->b;
   struct ebpb34 *ext = (void *)b;

   if (ext->boot_sig != 0x28 && ext->boot_sig != 0x29) {

      if (!nfo->bpb_checked) {
         WARNING("Unsupported BPB signature: %#x\n", ext->boot_sig);

         if (nfo->jmp[0] != 0xEB)
            WARNING("Very likely this MBR does NOT contain a BPB at all.\n");
         else
            WARNING("The BPB is likely a DOS 7.1 EBPB\n");

         nfo->bpb_checked = true;
      }

      /* Not a valid or supported BPB */
      return 1;
   }

   if (nfo->bpb_checked) {
      return nfo->bpb_broken ? -1 : 0;
   }
   nfo->bpb_checked = true;

   if (!b->sector_size) {
      ERROR("Invalid BIOS Parameter Block: missing sector size\n");
      nfo->bpb_broken = true;
      return -1;
   }

   if (b->sector_size > 4096) {
      ERROR("Invalid BIOS Parameter Block: invalid sector size: %u\n",
            b->sector_size);
      nfo->bpb_broken = true;
      return -1;
   }

   if (b->sector_size != 512) {
      WARNING("Sector size != 512: %u\n", b->sector_size);
   }

   if (!b->heads_per_cyl) {
      ERROR("Invalid BIOS Parameter Block: missing heads per cylinder\n");
      nfo->bpb_broken = true;
      return -1;
   }

   if (b->heads_per_cyl > 255) {
      ERROR("Invalid BIOS Parameter Block: invalid heads/cyl: %u\n",
            b->heads_per_cyl);
      nfo->bpb_broken = true;
      return -1;
   }

   if (!b->sectors_per_track) {
      ERROR("Invalid BIOS Parameter Block: missing sectors per track\n");
      nfo->bpb_broken = true;
      return -1;
   }

   if (b->sectors_per_track > 63) {
      ERROR("Invalid BIOS Parameter Block: invalid sectors/track: %u\n",
            b->sectors_per_track);
      nfo->bpb_broken = true;
      return -1;
   }

   if (!b->res_sectors) {
      ERROR("Invalid BIOS Parameter Block: resSectors == 0. Must be >= 1.\n");
      nfo->bpb_broken = true;
      return -1;
   }

   if (!b->small_sector_count && !b->large_sector_count) {
      ERROR("Invalid BIOS Parameter Block: missing sector count\n");
      nfo->bpb_broken = true;
      return -1;
   }

   return 0;
}

struct cmd {
   const char *name;
   int params;
   int (*func)(struct mbr_info *i, char **);
};

static bool opt_quiet;
static bool opt_force;

static void show_help_and_exit(int argc, char **argv)
{
   printf("Syntax:\n");
   printf("\t%s [-q][-f] <file> <command> [<cmd args...>]\n", argv[0]);
   printf("\n");
   printf("Commands:\n");
   printf("\tinfo                     \tDump BPB and list partitions\n");
   printf("\tclear                    \tRemove all the partitions\n");
   printf("\tcheck                    \tDo sanity checks\n");
   printf("\tremove <n>               \tRemove the partition <n> (1-4)\n");
   printf("\tboot <n>                 \tMake the partition <n> bootable\n");
   printf("\tadd <type> <first> <last>\tAdd a new partition in a free slot\n");
   printf("\tbpb s h spt tS rS hS sn  \tWrite the Bios Parameter Block\n");
   printf("\tmbr                      \tWrite a standard MBR\n");
   printf("\n");
   printf("Notes:\n");
   printf("\t- Both `first` and `last` are expressed in sectors\n");
   printf("\t- `last` can turned into `length` by prefixing it with `+`\n");
   printf("\t- The `length` param is KB or MB when followed by K, M (+2M)\n");
   printf("\t- `Type` is supposed to be a hex byte, like 0xC\n");
   printf("\t- `s`   = sector size\n");
   printf("\t- `h`   = heads per cylinder\n");
   printf("\t- `spt` = sectors per track\n");
   printf("\t- `tS`  = total sectors\n");
   printf("\t- `rS`  = reserved sectors\n");
   printf("\t- `hS`  = hidden sectors\n");
   printf("\t- `sn`  = serial number / disk UUID (hex)\n");
   printf("\n");
   exit(1);
}

static int find_first_free(struct mbr_part_table *t)
{
   for (int i = 0; i < 4; i++)
      if (t->partitions[i].id == 0)
         return i;

   return -1;
}

static int
parse_new_part_params(struct bpb *b, char **argv, u8 *rt, u32 *rs, u32 *re)
{
   unsigned long type, start, end;
   char *endp, *end_str = argv[2];
   bool end_is_size = false;

   errno = 0;
   type = strtoul(argv[0], NULL, 16);

   if (errno) {
      ERROR("Invalid type param (%s). Expected a hex num\n", argv[0]);
      return -1;
   }

   if (!IN_RANGE_INC(type, 0x1, 0xff)) {
      ERROR("Invalid type param. Range: 0x01 - 0xff\n");
      return -1;
   }

   if (argv[1][0] == '+') {
      ERROR("Invalid <start sector> param (%s)\n", argv[1]);
      return -1;
   }

   errno = 0;
   start = strtoul(argv[1], NULL, 10);

   if (errno) {
      ERROR("Invalid <start sector> param (%s)\n", argv[1]);
      return -1;
   }

   if (start > 0xffffffff) {
      ERROR("start sector cannot fit in LBA (32-bit)\n");
      return -1;
   }

   if (start < b->res_sectors) {
      ERROR("start sector falls in reserved area\n");
      return -1;
   }

   if (start >= bpb_get_sectors_count(b)) {
      ERROR("start sector is too big for this disk\n");
      return -1;
   }

   if (*end_str == '+') {
      end_is_size = true;
      end_str++;
   }

   errno = 0;
   end = strtoul(end_str, &endp, 10);

   if (errno) {
      ERROR("Invalid <end sector> param (%s)\n", argv[1]);
      return -1;
   }

   if (end_is_size) {

      /* Calculate the actual value of `end` */

      if (*endp == 'K')
         end *= 1024 / b->sector_size;
      else if (*endp == 'M')
         end *= (1024 / b->sector_size) * 1024;

      end += start - 1;
   }

   if (end > 0xffffffff) {
      ERROR("end sector cannot fit in LBA (32-bit)\n");
      return -1;
   }

   if (end < b->res_sectors) {
      ERROR("end sector falls in reserved area\n");
      return -1;
   }

   if (end >= bpb_get_sectors_count(b)) {
      ERROR("start sector is too big for this disk\n");
      return -1;
   }

   if (start > end) {
      ERROR("start (%lu) > end (%lu)\n", start, end);
      return -1;
   }

   *rt = type;
   *rs = start;
   *re = end;
   return 0;
}

static void
do_set_part(struct bpb *b,
            struct mbr_part_table *t,
            int n,
            u8 type,
            u32 start,
            u32 end)
{
   struct mbr_part *p = &t->partitions[n];
   u16 c, h, s;

   p->boot = 0;
   p->id = type;

   lba_to_chs(b, start, &c, &h, &s);
   p->start_head = h;
   p->start_sec = s;
   set_start_cyl(p, c);

   lba_to_chs(b, end, &c, &h, &s);
   p->end_head = h;
   p->end_sec = s;
   set_end_cyl(p, c);

   p->lba_start = start;
   p->lba_tot = end - start + 1;
}

static int
find_overlapping_part(struct bpb *b,
                      struct mbr_part_table *t,
                      u32 start, u32 end)
{
   for (int i = 0; i < 4; i++) {

      if (!t->partitions[i].id)
         continue;

      struct mbr_part *p = &t->partitions[i];

      u32 p_start = get_part_start(b, p);
      u32 p_end = get_part_end(b, p);

      if (IN_RANGE_INC(start, p_start, p_end) ||
          IN_RANGE_INC(end, p_start, p_end))
      {
         return i;
      }
   }

   return -1;
}

static int cmd_add(struct mbr_info *nfo, char **argv)
{
   struct bpb *b = nfo->b;
   struct mbr_part_table *t = nfo->t;

   u32 start, end;
   int n, overlap;
   u8 type;

   if (bpb_check(nfo) != 0) {
      ERROR("Unable to add partitions: unknown disk CHS geometry.\n");
      return 1;
   }

   n = find_first_free(t);

   if (n < 0) {
      ERROR("No free partition slot\n");
      return 1;
   }

   if (parse_new_part_params(b, argv, &type, &start, &end))
      return 1;

   overlap = find_overlapping_part(b, t, start, end);

   if (overlap >= 0) {
      ERROR("the new partition overlaps with partition %d\n", overlap + 1);
      return 1;
   }

   do_set_part(b, t, n, type, start, end);

   if (n == 0)
      t->partitions[0].boot = 0x80;

   return 0;
}

static int cmd_remove(struct mbr_info *nfo, char **argv)
{
   struct bpb *b = nfo->b;
   struct mbr_part_table *t = nfo->t;
   int num = atoi(argv[0]);

   if (bpb_check(nfo) < 0)
      return 1;

   if (!IN_RANGE_INC(num, 1, 4)) {
      ERROR("Invalid partition number. Valid range: 1-4.\n");
      return 1;
   }

   memset(&t->partitions[num - 1], 0, sizeof(struct mbr_part));
   return 0;
}

static int cmd_boot(struct mbr_info *nfo, char **argv)
{
   struct bpb *b = nfo->b;
   struct mbr_part_table *t = nfo->t;
   int num = atoi(argv[0]);
   struct mbr_part *p;

   if (bpb_check(nfo) < 0)
      return 1;

   if (!IN_RANGE_INC(num, 1, 4)) {
      ERROR("Invalid partition number. Valid range: 1-4.\n");
      return 1;
   }

   num--;
   p = &t->partitions[num];

   if (!p->id) {
      ERROR("partition %d is UNUSED\n", num + 1);
      return 1;
   }

   for (int i = 0; i < 4; i++) {

      if (i == num)
         t->partitions[i].boot = 0x80;
      else
         t->partitions[i].boot = 0;
   }

   return 0;
}

static int do_check_partitions(struct mbr_info *nfo, struct mbr_part_table *t)
{
   struct bpb *b = nfo->b;
   int fail = 0;

   if (bpb_check(nfo) != 0) {
      WARNING("Skip CHS <-> LBA check as the disk geometry is unknown!\n");
      return 0;
   }

   for (int i = 0; i < 4; i++) {

      struct mbr_part *p = &t->partitions[i];

      if (p->id == 0)
         continue;

      u32 s = chs_to_lba(b, get_start_cyl(p), p->start_head, p->start_sec);
      u32 e = chs_to_lba(b, get_end_cyl(p), p->end_head, p->end_sec);
      u32 end_lba = p->lba_start + p->lba_tot - 1;

      if (s != p->lba_start) {

         printf("WARNING: for partition %d, "
                "CHS start (%u,%u,%u) -> %u "
                "DIFFERS FROM declared LBA: %u\n",
                i+1, get_start_cyl(p), p->start_head, p->start_sec,
                s, p->lba_start);

         fail = 1;

      } else if (e != end_lba) {

         /*
          * It makes no sense to check for this condition when
          * s != p->lba_start, because shifting the `start` automatically
          * shifts the end too. Just check for this only when `start` matches.
          */

         printf("WARNING: for partition %d, "
                "CHS end (%u,%u,%u) -> %u "
                "DIFFERS FROM declared LBA: %u\n",
                i+1, get_end_cyl(p), p->end_head, p->end_sec,
                e, end_lba);

         fail = 1;
      }
   }

   return fail;
}

static int cmd_info(struct mbr_info *nfo, char **argv)
{
   struct bpb *b = nfo->b;
   struct mbr_part_table *t = nfo->t;

   if (bpb_check(nfo) == 0) {
      dump_bpb(b);
   }

   printf("\n");
   printf("Disk UUID (if valid):\n");
   printf("    Value:           %#9x\n", nfo->disk_uuid);

   printf("\n");
   printf("Partitions:\n\n");

   printf("    "
          " n | boot | type |   start (chs)    |    end (chs)     |"
          "      lba       | other \n");

   printf("    "
          "---+------+------+------------------+------------------+"
          "----------------+--------------------------------\n");

   for (int i = 0; i < 4; i++) {

      struct mbr_part *p = &t->partitions[i];

      if (p->id == 0) {
         printf("     %d | <unused>\n", i+1);
         continue;
      }

      printf("     %d | %s | 0x%02x | "
             "(%5u, %3u, %2u) | (%5u, %3u, %2u) | "
             "[%5u, %5u] | tot: %5u -> %u KB\n",
             i+1, p->boot & 0x80 ? "  * " : "    ", p->id,
             get_start_cyl(p), p->start_head, p->start_sec,
             get_end_cyl(p), p->end_head, p->end_sec,
             p->lba_start,
             p->lba_start + p->lba_tot - 1,
             p->lba_tot, p->lba_tot * b->sector_size / 1024);
   }

   printf("\n");
   do_check_partitions(nfo, t);
   return 0;
}

static int cmd_clear(struct mbr_info *nfo, char **argv)
{
   struct bpb *b = nfo->b;
   struct mbr_part_table *t = nfo->t;

   if (bpb_check(nfo) < 0)
      return 1;

   memset(t, 0, sizeof(*t));
   return 0;
}

static int cmd_check(struct mbr_info *nfo, char **argv)
{
   struct bpb *b = nfo->b;
   struct mbr_part_table *t = nfo->t;

   if (bpb_check(nfo) < 0)
      return 1;

   return do_check_partitions(nfo, t);
}

static int cmd_bpb(struct mbr_info *nfo, char **argv)
{
   struct bpb *b = nfo->b;
   struct ebpb34 *e = (void *)b;
   unsigned long val, bpb_space;

   if (nfo->jmp[0] == 0xEB && IN_RANGE(nfo->jmp[1], 12, 128)) {

      /*
       * There's likely already another BPB here or a hole for a BPB.
       * Let's determine the available space.
       */

      bpb_space = (u32)nfo->jmp[1] - 11 + 2; /* Off. rel. to the next instr. */

   } else {

      /*
       * Cannot find a valid initial jmp instruction.
       * Let's use the space we need. We have no other choice.
       */
      bpb_space = sizeof(*e);


      if (!opt_force) {
         if (nfo->jmp[0] != 0) {

            WARNING("Possibly overriding an MBR without BPB\n");

            if (nfo->jmp[0] == 0xFA)
               WARNING("This 1st byte looks like a x86 CLI instruction\n");
         }
      }
   }

   if (bpb_space < sizeof(*e)) {
      ERROR("Available space for BPB: %lu B < %lu B\n", bpb_space, sizeof(*e));
      ERROR("Writing a EBPB 3.4 will certaily override MBR code\n");
      ERROR("Stop. Nothing was written.\n");
      return 1;
   }

   /* zero the whole available space as we won't set all of its fields */
   memset(e, 0, bpb_space);

   e->bpb.media_type = 0xf0;  /* floppy (good even if media is a USB stick) */
   e->drive_num = 0x80;       /* standard value for first the disk */
   e->boot_sig = 0x28;        /* DOS 3.4 EBPB */

   {
      const char *str_val = argv[0];
      errno = 0;
      val = strtoul(str_val, NULL, 10);

      if (errno || val > 4096) {
         ERROR("Invalid sector size value '%s'\n", str_val);
         return 1;
      }

      e->bpb.sector_size = val;
   }

   {
      const char *str_val = argv[1];
      errno = 0;
      val = strtoul(str_val, NULL, 10);

      if (errno || val > 255) {
         ERROR("Invalid heads per cylinder value '%s'\n", str_val);
         return 1;
      }

      e->bpb.heads_per_cyl = val;
   }

   {
      const char *str_val = argv[2];
      errno = 0;
      val = strtoul(str_val, NULL, 10);

      if (errno || val > 255) {
         ERROR("Invalid sectors per track value '%s'\n", str_val);
         return 1;
      }

      e->bpb.sectors_per_track = val;
   }

   {
      const char *str_val = argv[3];
      errno = 0;
      val = strtoul(str_val, NULL, 10);

      if (errno) {
         ERROR("Invalid total sectors value '%s'\n", str_val);
         return 1;
      }

      e->bpb.large_sector_count = val;
   }

   {
      const char *str_val = argv[4];
      errno = 0;
      val = strtoul(str_val, NULL, 10);

      if (errno || val == 0) {
         ERROR("Invalid reserved sectors value '%s'\n", str_val);
         return 1;
      }

      e->bpb.res_sectors = val;
   }

   {
      const char *str_val = argv[5];
      errno = 0;
      val = strtoul(str_val, NULL, 10);

      if (errno) {
         ERROR("Invalid hidden sectors value '%s'\n", str_val);
         return 1;
      }

      e->bpb.hidden_sectors = val;
   }

   {
      const char *str_val = argv[6];
      errno = 0;
      val = strtoul(str_val, NULL, 16);

      if (errno) {
         ERROR("Invalid serial/UUID value '%s'\n", str_val);
         return 1;
      }

      e->serial = val;

      /* Keep BPB's serial in sync with the UUID field */
      nfo->disk_uuid = val;
   }

   return 0;
}

static int cmd_mbr(struct mbr_info *nfo, char **argv)
{
   /*
    * Communicate to main() that we'll rewrite the whole MBR.
    */
   nfo->b = NULL;
   nfo->t = NULL;

   memcpy(nfo->raw_buf, mbr, sizeof(mbr));
   return 0;
}

#define DECL_CMD(nn, par) {.name = #nn, .params = par, .func = &cmd_##nn}

const static struct cmd cmds[] =
{
   DECL_CMD(info, 0),
   DECL_CMD(clear, 0),
   DECL_CMD(check, 0),
   DECL_CMD(remove, 1),
   DECL_CMD(add, 3),
   DECL_CMD(boot, 1),
   DECL_CMD(bpb, 7),
   DECL_CMD(mbr, 0),
};

static int
parse_opts(int argc, char ***r_argv, struct cmd *c_ref, const char **file_ref)
{
   char **argv = *r_argv;
   const char *cmdname;
   struct cmd cmd = {0};
   int i;

   if (argc <= 2)
      return -1;

   /* Forget about the 1st parameter (cmd line) */
   argc--; argv++;

   if (!strcmp(argv[0], "-q")) {
      opt_quiet = true;
      argc--; argv++;
   }

   if (!strcmp(argv[0], "-f")) {
      opt_force = true;
      argc--; argv++;
   }

   if (argc < 2)
      return -1;     /* too few args */

   *file_ref = argv[0];
   cmdname = argv[1];

   for (i = 0; i < ARRAY_SIZE(cmds); i++) {

      if (!strcmp(cmdname, cmds[i].name)) {
         cmd = cmds[i];
         break;
      }
   }

   if (i == ARRAY_SIZE(cmds))
      return -1;     /* unknown command */

   argc -= 2; /* file and cmd name */
   argv += 2; /* file and cmd name */

   if (argc < cmd.params)
      return -1;     /* too few args */

   *c_ref = cmd;
   *r_argv = argv;
   return 0;
}

int main(int argc, char **argv)
{
   const char *file;
   struct cmd cmd;
   char buf[512];
   FILE *fh;
   size_t r;
   int rc;
   struct mbr_part_table t;
   char bpb_buf[MAX_EBPB_SIZE] ALIGNED_AT(4);
   bool write_back = false;
   struct mbr_info i = {
      .raw_buf = buf,
      .b = (void *)bpb_buf,
      .t = (void *)&t,
      .disk_uuid = 0,
      .bpb_checked = false,
   };

   STATIC_ASSERT(sizeof(t.partitions[0]) == 16);
   STATIC_ASSERT(sizeof(t) == 64);  /* 4 x 16 bytes */
   STATIC_ASSERT(sizeof(buf) == sizeof(mbr));

   if (parse_opts(argc, &argv, &cmd, &file) < 0)
      show_help_and_exit(argc, argv);

   fh = fopen(file, "r+b");

   if (!fh) {
      ERROR("Failed to open file '%s'\n", file);
      return 1;
   }

   r = fread(buf, 1, 512, fh);

   if (r != 512) {
      ERROR("Failed to read the first 512 bytes\n");
      return 1;
   }

   memcpy(i.jmp, buf + 0, sizeof(i.jmp));
   memcpy(bpb_buf, buf + BPB_OFFSET, sizeof(bpb_buf));
   memcpy(&t, buf + PART_TABLE_OFFSET, sizeof(t));
   memcpy(&i.disk_uuid, buf + DISK_UUID_OFFSET, sizeof(i.disk_uuid));

   /* Call func with its specific params */
   rc = cmd.func(&i, argv);

   if (!rc) {

      if (i.b) {

         if (memcmp(bpb_buf, buf + BPB_OFFSET, sizeof(bpb_buf))) {

            INFO("BIOS parameter block changed, write it back\n");
            memcpy(buf + BPB_OFFSET, bpb_buf, sizeof(bpb_buf));
            write_back = true;
         }

         if (memcmp(&t, buf + PART_TABLE_OFFSET, sizeof(t))) {
            INFO("Partition table changed, write it back\n");
            memcpy(buf + PART_TABLE_OFFSET, &t, sizeof(t));
            write_back = true;
         }

         if (memcmp(&i.disk_uuid, buf + DISK_UUID_OFFSET, sizeof(i.disk_uuid))) {
            INFO("DISK UUID changed, write it back\n");
            memcpy(buf + DISK_UUID_OFFSET, &i.disk_uuid, sizeof(i.disk_uuid));
            write_back = true;
         }

      } else {

         INFO("The whole MBR was re-written\n");
         write_back = true;
      }
   }

   if (write_back) {

      if (fseek(fh, 0, SEEK_SET) < 0) {
         ERROR("fseek() failed\n");
         goto end;
      }

      r = fwrite(buf, 1, 512, fh);

      if (r != 512) {
         ERROR("Failed to write the first 512 bytes\n");
         rc = 1;
      }
   }

end:
   fclose(fh);
   return rc;
}
