/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/fat32_base.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAX_ACTIONS                                     3
#define NO_ACTIONS()           { NULL, NULL, NULL, NULL }
#define ACTIONS_1(a1)          {   a1, NULL, NULL, NULL }
#define ACTIONS_2(a1, a2)      {   a1,   a2, NULL, NULL }
#define ACTIONS_3(a1, a2, a3)  {   a1,   a2,   a3, NULL }

struct action_ctx {
   int fd;
   void *vaddr;
   struct stat statbuf;
};

typedef int (*act_t)(struct action_ctx *);

struct action {

   const char *params[2];
   act_t pre_mmap_actions[MAX_ACTIONS+1];
   act_t actions[MAX_ACTIONS+1];
   act_t post_munmap_actions[MAX_ACTIONS+1];
};

/* Global variables */

static u32 used_bytes;
static u32 ff_clu_off;

/* --- */

static int action_mmap(struct action_ctx *ctx)
{
   ASSERT(ctx->vaddr == NULL);
   ASSERT(ctx->fd > 0);
   ASSERT(ctx->statbuf.st_size > 0);

   ctx->vaddr = mmap(NULL,                           /* addr */
                     ctx->statbuf.st_size,           /* length */
                     PROT_READ|PROT_WRITE,           /* prot */
                     MAP_SHARED,                     /* flags */
                     ctx->fd,                        /* fd */
                     0);                             /* offset */

   if (ctx->vaddr == (void *)-1) {
      perror("mmap() failed");
      return -1;
   }

   return 0;
}

static int action_munmap(struct action_ctx *ctx)
{
   int rc = munmap(ctx->vaddr, ctx->statbuf.st_size);

   if (rc < 0) {
      perror("munmap() failed");
      return -1;
   }

   ctx->vaddr = NULL;
   return 0;
}

static int action_calc_used_bytes(struct action_ctx *ctx)
{
   ff_clu_off = fat_get_first_free_cluster_off(ctx->vaddr);
   used_bytes = fat_calculate_used_bytes(ctx->vaddr);

   if (ff_clu_off > used_bytes) {

      fprintf(stderr,
              "FATAL ERROR: ff_clu_off (%u) > used_bytes (%u)\n",
              ff_clu_off, used_bytes);

      return 1;
   }

   return 0;
}

static int action_truncate(struct action_ctx *ctx)
{
   if (ftruncate(ctx->fd, used_bytes) < 0) {
      perror("ftruncate() failed");
      return 1;
   }

   return 0;
}

static int action_print_used_bytes(struct action_ctx *ctx)
{
   printf("%u\n", used_bytes);
   return 0;
}

static int action_expand_file_by_4k(struct action_ctx *ctx)
{
   if (ftruncate(ctx->fd, ctx->statbuf.st_size + 4096) < 0) {
      perror("ftruncate() failed");
      return 1;
   }

   if (fstat(ctx->fd, &ctx->statbuf) < 0) {
      perror("stat() failed");
      return 1;
   }

   return 0;
}

static int action_do_align(struct action_ctx *ctx)
{
   if (used_bytes > ctx->statbuf.st_size) {
      fprintf(stderr,
              "FATAL ERROR: used bytes (%u) > st_size (%ld)\n",
              used_bytes, ctx->statbuf.st_size);
      return 1;
   }

   if (fat_is_first_data_sector_aligned(ctx->vaddr, 4096)) {
      printf("INFO: First data sector already aligned\n");
      return 0;
   }

   if (used_bytes < ctx->statbuf.st_size) {
      fprintf(stderr, "INFO: fat file NOT truncated.\n");
      fprintf(stderr, "INFO: used bytes (%u) < st_size (%ld)\n",
              used_bytes, ctx->statbuf.st_size);
   }

   /*
    * While on the Linux kernel (and on Tilck as well) it's fine to expand or
    * shrink a file while being memory-mapped, that's NOT true on WSL. While
    * it's true that WSL is not supposed to be Tilck's main build host type,
    * it is still nice to support it, in particular because that does not
    * require a huge effort; just minor fixes here and there.
    */
   if (action_munmap(ctx) < 0)
      return -1;

   action_expand_file_by_4k(ctx);

   if (action_mmap(ctx) < 0)
      return -1;

   fat_align_first_data_sector(ctx->vaddr, 4096);
   return 0;
}

struct action actions[] = {

   {
      {"-t", "--truncate"},
      NO_ACTIONS(),
      ACTIONS_1(action_calc_used_bytes),
      ACTIONS_1(action_truncate),
   },

   {
      {"-c", "--calc_used_bytes"},
      NO_ACTIONS(),
      ACTIONS_1(action_calc_used_bytes),
      ACTIONS_1(action_print_used_bytes),
   },

   {
      {"-a", "--align_first_data_sector"},
      NO_ACTIONS(),
      ACTIONS_2(action_calc_used_bytes, action_do_align),
      NO_ACTIONS(),
   },
};

void show_help_and_exit(int argc, char **argv)
{
   printf("Syntax:\n");
   printf("    %s -t, --truncate <fat part file>\n", argv[0]);
   printf("    %s -c, --calc_used_bytes <fat part file>\n", argv[0]);
   printf("    %s -a, --align_first_data_sector <fat part file>\n", argv[0]);
   exit(1);
}

static int
parse_opts(int argc, char **argv, struct action **a_ref, const char **file_ref)
{
   struct action *a = NULL;

   if (argc < 3)
      return -1;

   for (int i = 0; !a && i < ARRAY_SIZE(actions); i++) {
      for (int j = 0; j < 2; j++)
         if (!strcmp(argv[1], actions[i].params[j]))
            a = &actions[i];
   }

   if (!a)
      return -1;

   *a_ref = a;
   *file_ref = argv[2];
   return 0;
}

int main(int argc, char **argv)
{
   int failed;
   const char *file;
   struct action *a;
   struct action_ctx ctx = {0};

   if (parse_opts(argc, argv, &a, &file) < 0)
      show_help_and_exit(argc, argv);

   if (stat(file, &ctx.statbuf) < 0) {
      perror("stat() failed");
      return 1;
   }

   if (!S_ISREG(ctx.statbuf.st_mode) && !S_ISBLK(ctx.statbuf.st_mode)) {
      fprintf(stderr, "Invalid file type\n");
      return 1;
   }

   ctx.fd = open(file, O_RDWR);

   if (ctx.fd < 0) {
      perror("open() failed");
      return 1;
   }

   for (act_t *f = a->pre_mmap_actions; *f; f++)
      if ((failed = (*f)(&ctx)))
         goto out;

   if (action_mmap(&ctx) < 0) {
      failed = 1;
      goto out;
   }

   for (act_t *f = a->actions; *f; f++)
      if ((failed = (*f)(&ctx)))
         goto out;

   if (action_munmap(&ctx) < 0) {
      failed = 1;
      goto out;
   }

   for (act_t *f = a->post_munmap_actions; *f; f++)
      if ((failed = (*f)(&ctx)))
         goto out;

out:
   close(ctx.fd);
   return failed;
}
