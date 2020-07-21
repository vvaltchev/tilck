/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct PACKED bpb34 {

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

static inline u32 bpb_get_sectors_count(struct bpb34 *b)
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

static u32 chs_to_lba(struct bpb34 *b, u32 c, u32 h, u32 s)
{
   return (c * b->heads_per_cyl + h) * b->sectors_per_track + (s - 1);
}

static void lba_to_chs(struct bpb34 *b, u32 lba, u16 *c, u16 *h, u16 *s)
{
   *c = lba / (b->heads_per_cyl * b->sectors_per_track);
   *h = (lba / b->sectors_per_track) % b->heads_per_cyl;
   *s = (lba % b->sectors_per_track) + 1;
}

static u32 get_part_start(struct bpb34 *b, struct mbr_part *p)
{
   return chs_to_lba(b, get_start_cyl(p), p->start_head, p->start_sec);
}

static u32 get_part_end(struct bpb34 *b, struct mbr_part *p)
{
   return chs_to_lba(b, get_end_cyl(p), p->end_head, p->end_sec);
}

struct PACKED mbr_part_table {
   struct mbr_part partitions[4];
};

struct cmd {
   const char *name;
   int params;
   int (*func)(struct bpb34 *, struct mbr_part_table *, char **);
};

static bool opt_quiet;

static void show_help_and_exit(int argc, char **argv)
{
   printf("Syntax:\n");
   printf("    %s [-q] <file> <command> [<cmd args...>]\n", argv[0]);
   printf("\n");
   printf("Commands:\n");
   printf("    list                      List MBR partitions\n");
   printf("    clear                     Remove all the partitions\n");
   printf("    check                     Do sanity checks\n");
   printf("    remove <n>                Remove the partition <n> (1-4)\n");
   printf("    boot <n>                  Make the partition <n> bootable\n");
   printf("    add <type> <start> <end>  Add a new partition in a free slot\n");
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
parse_new_part_params(struct bpb34 *b, char **argv, u8 *rt, u32 *rs, u32 *re)
{
   unsigned long type, start, end;
   char *endp, *end_str = argv[2];
   bool end_is_size = false;

   errno = 0;
   type = strtoul(argv[0], NULL, 16);

   if (errno) {
      fprintf(stderr, "Invalid type param (%s). Expected a hex num\n", argv[0]);
      return -1;
   }

   if (!IN_RANGE_INC(type, 0x1, 0xff)) {
      fprintf(stderr, "Invalid type param. Range: 0x01 - 0xff\n");
      return -1;
   }

   if (argv[1][0] == '+') {
      fprintf(stderr, "Invalid <start sector> param (%s)\n", argv[1]);
      return -1;
   }

   errno = 0;
   start = strtoul(argv[1], NULL, 10);

   if (errno) {
      fprintf(stderr, "Invalid <start sector> param (%s)\n", argv[1]);
      return -1;
   }

   if (start > 0xffffffff) {
      fprintf(stderr, "ERROR: start sector cannot fit in LBA (32-bit)\n");
      return -1;
   }

   if (start < b->res_sectors) {
      fprintf(stderr, "ERROR: start sector falls in reserved area\n");
      return -1;
   }

   if (start >= bpb_get_sectors_count(b)) {
      fprintf(stderr, "ERROR: start sector is too big for this disk\n");
      return -1;
   }

   if (*end_str == '+') {
      end_is_size = true;
      end_str++;
   }

   errno = 0;
   end = strtoul(end_str, &endp, 10);

   if (errno) {
      fprintf(stderr, "Invalid <end sector> param (%s)\n", argv[1]);
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
      fprintf(stderr, "ERROR: end sector cannot fit in LBA (32-bit)\n");
      return -1;
   }

   if (end < b->res_sectors) {
      fprintf(stderr, "ERROR: end sector falls in reserved area\n");
      return -1;
   }

   if (end >= bpb_get_sectors_count(b)) {
      fprintf(stderr, "ERROR: start sector is too big for this disk\n");
      return -1;
   }

   if (start > end) {
      fprintf(stderr, "ERROR: start (%lu) > end (%lu)\n", start, end);
      return -1;
   }

   *rt = type;
   *rs = start;
   *re = end;
   return 0;
}

static void
do_set_part(struct bpb34 *b,
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
find_overlapping_part(struct bpb34 *b,
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

static int cmd_add(struct bpb34 *b, struct mbr_part_table *t, char **argv)
{
   u8 type;
   u32 start, end;
   int n = find_first_free(t);
   int overlap;

   if (n < 0) {
      fprintf(stderr, "No free partition slot\n");
      return 1;
   }

   if (parse_new_part_params(b, argv, &type, &start, &end))
      return 1;

   overlap = find_overlapping_part(b, t, start, end);

   if (overlap >= 0) {

      fprintf(stderr,
              "ERROR: the new partition would overlap with partition %d\n",
              overlap + 1);

      return 1;
   }

   do_set_part(b, t, n, type, start, end);

   if (n == 0)
      t->partitions[0].boot = 0x80;

   return 0;
}

static int cmd_remove(struct bpb34 *b, struct mbr_part_table *t, char **argv)
{
   int num = atoi(argv[0]);

   if (!IN_RANGE_INC(num, 1, 4)) {
      fprintf(stderr, "Invalid partition number. Valid range: 1-4.\n");
      return 1;
   }

   memset(&t->partitions[num - 1], 0, sizeof(struct mbr_part));
   return 0;
}

static int cmd_boot(struct bpb34 *b, struct mbr_part_table *t, char **argv)
{
   int num = atoi(argv[0]);
   struct mbr_part *p;

   if (!IN_RANGE_INC(num, 1, 4)) {
      fprintf(stderr, "Invalid partition number. Valid range: 1-4.\n");
      return 1;
   }

   num--;
   p = &t->partitions[num];

   if (!p->id) {
      fprintf(stderr, "ERROR: partition %d is UNUSED\n", num + 1);
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

static int do_check_partitions(struct bpb34 *b, struct mbr_part_table *t)
{
   int fail = 0;

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

static int cmd_list(struct bpb34 *b, struct mbr_part_table *t, char **argv)
{
   printf("\n");
   printf("Bios Parameter Block:\n");
   printf("    Sector size:       %u\n", b->sector_size);
   printf("    Heads per cyl:     %u\n", b->heads_per_cyl);
   printf("    Sectors per track: %u\n", b->sectors_per_track);
   printf("    Reserved sectors:  %u\n", b->res_sectors);
   printf("    Hidden sectors:    %u\n", b->hidden_sectors);
   printf("    Tot sectors count: %u\n", bpb_get_sectors_count(b));

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
   do_check_partitions(b, t);
   return 0;
}

static int cmd_clear(struct bpb34 *b, struct mbr_part_table *t, char **argv)
{
   memset(t, 0, sizeof(*t));
   return 0;
}

static int cmd_check(struct bpb34 *b, struct mbr_part_table *t, char **argv)
{
   return do_check_partitions(b, t);
}

const static struct cmd cmds[] =
{
   {
      .name = "list",
      .params = 0,
      .func = &cmd_list,
   },

   {
      .name = "clear",
      .params = 0,
      .func = &cmd_clear,
   },

   {
      .name = "check",
      .params = 0,
      .func = &cmd_check,
   },

   {
      .name = "remove",
      .params = 1,
      .func = &cmd_remove,
   },

   {
      .name = "add",
      .params = 3,
      .func = &cmd_add,
   },

   {
      .name = "boot",
      .params = 1,
      .func = &cmd_boot,
   },
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
   struct bpb34 b;

   bool write_back = false;

   STATIC_ASSERT(sizeof(t.partitions[0]) == 16);
   STATIC_ASSERT(sizeof(t) == 64);  /* 4 x 16 bytes */

   if (parse_opts(argc, &argv, &cmd, &file) < 0)
      show_help_and_exit(argc, argv);

   fh = fopen(file, "r+b");

   if (!fh) {
      fprintf(stderr, "Failed to open file '%s'\n", file);
      return 1;
   }

   r = fread(buf, 1, 512, fh);

   if (r != 512) {
      fprintf(stderr, "Failed to read the first 512 bytes\n");
      return 1;
   }

   memcpy(&b, buf + 0xb, sizeof(b));
   memcpy(&t, buf + 0x1be, sizeof(t));

   if (b.boot_sig != 0x28 && b.boot_sig != 0x29) {
      fprintf(stderr, "Unsupported MBR boot signature: 0x%x\n", b.boot_sig);
      goto end;
   }

   /* Call func with its specific params */
   rc = cmd.func(&b, &t, argv);

   if (!rc) {

      if (memcmp(&t, buf + 0x1be, 64)) {

         if (!opt_quiet)
            printf("INFO: partition table changed, write it back\n");

         memcpy(buf + 0x1be, &t, 64);
         write_back = true;
      }
   }

   if (write_back) {

      if (fseek(fh, 0, SEEK_SET) < 0) {
         fprintf(stderr, "fseek() failed\n");
         goto end;
      }

      r = fwrite(buf, 1, 512, fh);

      if (r != 512) {
         fprintf(stderr, "Failed to write the first 512 bytes\n");
         rc = 1;
      }
   }

end:
   fclose(fh);
   return rc;
}
