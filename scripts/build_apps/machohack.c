/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * machohack: Mach-O symbol manipulation tool for unit test mocking.
 *
 * For each specified symbol, this tool:
 *   1. Renames the defined symbol _sym -> ___real_sym (preserving code)
 *   2. Adds an undefined _sym entry
 *   3. Patches all relocations from the old _sym to the new undefined
 *
 * After patching:
 *   - Intra-TU calls to _sym resolve to the undefined entry, which the
 *     linker satisfies with the test's strong definition.
 *   - ___real_sym remains a strong global pointing to the original code.
 *
 * This achieves the same result as:
 *    objcopy --weaken-symbol=sym --add-symbol=__real_sym=.text:addr
 * but without Mach-O weak coalescing eliminating the original code.
 *
 * Usage:
 *    machohack <obj.o> --patch-syms <sym> [<sym> ...]
 *    machohack <obj.o> --list-text-syms
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/reloc.h>

#define MAX_SYMS 256

struct patch_info {
   uint32_t old_sym_idx;   /* original nlist index of _sym */
   uint32_t new_sym_idx;   /* new nlist index for undefined _sym */
   char real_name[512];    /* "___real_<name>" */
   char orig_name[512];    /* "_<name>" */
};

static void
usage(const char *prog)
{
   fprintf(stderr,
      "Usage:\n"
      "  %s <obj.o> --patch-syms <sym> [<sym> ...]\n"
      "  %s <obj.o> --list-text-syms\n",
      prog, prog);
   exit(1);
}

static void *
read_file(const char *path, size_t *out_size)
{
   FILE *f = fopen(path, "rb");
   struct stat st;
   void *buf;

   if (!f) { perror(path); return NULL; }

   fstat(fileno(f), &st);
   buf = malloc((size_t)st.st_size);

   if (!buf) { fclose(f); return NULL; }

   fread(buf, 1, (size_t)st.st_size, f);
   fclose(f);
   *out_size = (size_t)st.st_size;
   return buf;
}

static int
write_file(const char *path, const void *data, size_t size)
{
   FILE *f = fopen(path, "wb");

   if (!f) { perror(path); return -1; }

   if (fwrite(data, 1, size, f) != size) {
      perror("fwrite");
      fclose(f);
      return -1;
   }

   fclose(f);
   return 0;
}

static struct symtab_command *
find_symtab_cmd(void *buf, size_t size, bool *is64, uint32_t *ncmds)
{
   uint32_t magic = *(uint32_t *)buf;
   struct load_command *lc;
   uint32_t n, i;
   size_t off;

   if (magic == MH_MAGIC_64) {
      struct mach_header_64 *h = buf;
      *is64 = true;
      n = h->ncmds;
      off = sizeof(*h);
   } else if (magic == MH_MAGIC) {
      struct mach_header *h = buf;
      *is64 = false;
      n = h->ncmds;
      off = sizeof(*h);
   } else {
      fprintf(stderr, "Not a Mach-O file (magic 0x%x)\n", magic);
      return NULL;
   }

   *ncmds = n;

   for (i = 0; i < n; i++) {
      lc = (struct load_command *)((char *)buf + off);
      if (lc->cmd == LC_SYMTAB)
         return (struct symtab_command *)lc;
      off += lc->cmdsize;
   }

   return NULL;
}

static struct dysymtab_command *
find_dysymtab_cmd(void *buf, bool is64, uint32_t ncmds)
{
   struct load_command *lc;
   size_t off;
   uint32_t i;

   off = is64 ? sizeof(struct mach_header_64) : sizeof(struct mach_header);

   for (i = 0; i < ncmds; i++) {
      lc = (struct load_command *)((char *)buf + off);
      if (lc->cmd == LC_DYSYMTAB)
         return (struct dysymtab_command *)lc;
      off += lc->cmdsize;
   }

   return NULL;
}

/*
 * Iterate over all sections and patch relocations that reference
 * old_idx to reference new_idx instead.
 */
static void
patch_relocs(void *buf, size_t size, bool is64, uint32_t ncmds,
             uint32_t old_idx, uint32_t new_idx)
{
   struct load_command *lc;
   size_t off;
   uint32_t i;

   off = is64 ? sizeof(struct mach_header_64) : sizeof(struct mach_header);

   for (i = 0; i < ncmds; i++) {

      lc = (struct load_command *)((char *)buf + off);

      if (lc->cmd == LC_SEGMENT_64) {

         struct segment_command_64 *seg = (void *)lc;
         struct section_64 *sect;
         uint32_t j;

         sect = (struct section_64 *)((char *)seg + sizeof(*seg));

         for (j = 0; j < seg->nsects; j++) {

            if (sect[j].nreloc == 0)
               continue;

            struct relocation_info *relocs;
            relocs = (void *)((char *)buf + sect[j].reloff);

            for (uint32_t k = 0; k < sect[j].nreloc; k++) {

               if (relocs[k].r_extern &&
                   relocs[k].r_symbolnum == old_idx) {
                  relocs[k].r_symbolnum = new_idx;
               }
            }
         }

      } else if (lc->cmd == LC_SEGMENT) {

         struct segment_command *seg = (void *)lc;
         struct section *sect;
         uint32_t j;

         sect = (struct section *)((char *)seg + sizeof(*seg));

         for (j = 0; j < seg->nsects; j++) {

            if (sect[j].nreloc == 0)
               continue;

            struct relocation_info *relocs;
            relocs = (void *)((char *)buf + sect[j].reloff);

            for (uint32_t k = 0; k < sect[j].nreloc; k++) {

               if (relocs[k].r_extern &&
                   relocs[k].r_symbolnum == old_idx) {
                  relocs[k].r_symbolnum = new_idx;
               }
            }
         }
      }

      off += lc->cmdsize;
   }
}

/*
 * Also patch relocations that reference any symbol index >= insert_point:
 * since we're inserting new entries, all subsequent indices shift.
 */
static void
shift_reloc_indices(void *buf, size_t size, bool is64, uint32_t ncmds,
                    uint32_t insert_point, uint32_t shift)
{
   struct load_command *lc;
   size_t off;
   uint32_t i;

   off = is64 ? sizeof(struct mach_header_64) : sizeof(struct mach_header);

   for (i = 0; i < ncmds; i++) {

      lc = (struct load_command *)((char *)buf + off);

      if (lc->cmd == LC_SEGMENT_64) {

         struct segment_command_64 *seg = (void *)lc;
         struct section_64 *sect;
         uint32_t j;

         sect = (struct section_64 *)((char *)seg + sizeof(*seg));

         for (j = 0; j < seg->nsects; j++) {

            if (sect[j].nreloc == 0)
               continue;

            struct relocation_info *relocs;
            relocs = (void *)((char *)buf + sect[j].reloff);

            for (uint32_t k = 0; k < sect[j].nreloc; k++) {

               if (relocs[k].r_extern &&
                   relocs[k].r_symbolnum >= insert_point) {
                  relocs[k].r_symbolnum += shift;
               }
            }
         }

      } else if (lc->cmd == LC_SEGMENT) {

         struct segment_command *seg = (void *)lc;
         struct section *sect;
         uint32_t j;

         sect = (struct section *)((char *)seg + sizeof(*seg));

         for (j = 0; j < seg->nsects; j++) {

            if (sect[j].nreloc == 0)
               continue;

            struct relocation_info *relocs;
            relocs = (void *)((char *)buf + sect[j].reloff);

            for (uint32_t k = 0; k < sect[j].nreloc; k++) {

               if (relocs[k].r_extern &&
                   relocs[k].r_symbolnum >= insert_point) {
                  relocs[k].r_symbolnum += shift;
               }
            }
         }
      }

      off += lc->cmdsize;
   }
}

static void
do_list_text_syms(void *buf, size_t size)
{
   struct symtab_command *sc;
   bool is64;
   uint32_t ncmds, i;

   sc = find_symtab_cmd(buf, size, &is64, &ncmds);
   if (!sc) return;

   const char *strtab = (char *)buf + sc->stroff;

   if (is64) {
      struct nlist_64 *syms = (void *)((char *)buf + sc->symoff);
      for (i = 0; i < sc->nsyms; i++) {
         if ((syms[i].n_type & N_TYPE) == N_SECT &&
             (syms[i].n_type & N_EXT))
            printf("%s\n", strtab + syms[i].n_un.n_strx);
      }
   } else {
      struct nlist *syms = (void *)((char *)buf + sc->symoff);
      for (i = 0; i < sc->nsyms; i++) {
         if ((syms[i].n_type & N_TYPE) == N_SECT &&
             (syms[i].n_type & N_EXT))
            printf("%s\n", strtab + syms[i].n_un.n_strx);
      }
   }
}

/*
 * For each named symbol:
 *   1. Rename the nlist entry: _sym -> ___real_sym (change n_strx)
 *   2. Insert a new undefined _sym nlist entry at the end (undef section)
 *   3. Patch all relocations from old index to the new undefined entry
 *
 * New undefined entries go at the end of the symbol table (after all
 * existing undefs), which satisfies LC_DYSYMTAB ordering.
 */
static int
do_patch_syms(const char *path, void *buf, size_t size,
              const char **sym_names, int nsym_names)
{
   struct symtab_command *sc;
   struct dysymtab_command *dsc;
   bool is64;
   uint32_t ncmds;
   int n_found = 0;
   struct patch_info patches[MAX_SYMS];

   sc = find_symtab_cmd(buf, size, &is64, &ncmds);

   if (!sc) {
      fprintf(stderr, "%s: no LC_SYMTAB\n", path);
      return 1;
   }

   dsc = find_dysymtab_cmd(buf, is64, ncmds);

   const char *strtab = (char *)buf + sc->stroff;
   size_t nlist_size = is64 ? sizeof(struct nlist_64) : sizeof(struct nlist);

   /* Find matching symbols and record their indices */
   for (int si = 0; si < nsym_names; si++) {

      char mname[512];
      snprintf(mname, sizeof(mname), "_%s", sym_names[si]);

      for (uint32_t i = 0; i < sc->nsyms; i++) {

         const char *name;
         uint8_t ntype;

         if (is64) {
            struct nlist_64 *s;
            s = (struct nlist_64 *)((char *)buf + sc->symoff) + i;
            ntype = s->n_type;
            name = strtab + s->n_un.n_strx;
         } else {
            struct nlist *s;
            s = (struct nlist *)((char *)buf + sc->symoff) + i;
            ntype = s->n_type;
            name = strtab + s->n_un.n_strx;
         }

         if (strcmp(name, mname) != 0)
            continue;
         if ((ntype & N_TYPE) != N_SECT || !(ntype & N_EXT))
            continue;

         patches[n_found].old_sym_idx = i;
         snprintf(patches[n_found].real_name,
                  sizeof(patches[0].real_name),
                  "___real_%s", sym_names[si]);
         snprintf(patches[n_found].orig_name,
                  sizeof(patches[0].orig_name),
                  "_%s", sym_names[si]);
         n_found++;
         break;
      }
   }

   if (n_found == 0)
      return write_file(path, buf, size);

   /*
    * Calculate sizes for the new file:
    * - n_found new nlist entries (undefined _sym)
    * - n_found new strings for "___real_<sym>" names
    * - n_found new strings for "_<sym>" (for the undefined entries)
    */
   size_t new_nlist_bytes = (size_t)n_found * nlist_size;
   size_t new_str_bytes = 0;

   for (int i = 0; i < n_found; i++) {
      new_str_bytes += strlen(patches[i].real_name) + 1;
      new_str_bytes += strlen(patches[i].orig_name) + 1;
   }

   size_t new_size = size + new_nlist_bytes + new_str_bytes;
   char *out = calloc(1, new_size);

   if (!out) { perror("calloc"); return 1; }

   /*
    * New undefined entries go at the very end of the symbol table.
    * This is after all existing symbols (locals + ext-defined + undefs).
    */
   uint32_t insert_idx = sc->nsyms;
   size_t symtab_start = sc->symoff;
   size_t old_symtab_end = symtab_start + sc->nsyms * nlist_size;

   /* Copy everything up to and including the original symbol table */
   memcpy(out, buf, old_symtab_end);

   /*
    * Step 1: Rename the original _sym -> ___real_sym in the string
    * table. We'll append the new name to the string table and update
    * n_strx in the copied nlist entries.
    */

   /* We need to figure out the new string offsets. The string table
    * will be shifted by new_nlist_bytes (because we're inserting nlist
    * entries before it). */
   uint32_t new_stroff = sc->stroff + (uint32_t)new_nlist_bytes;
   uint32_t str_cursor = sc->strsize;

   /* For each matched symbol, update its n_strx to point to the new
    * "___real_*" name that will be appended to the string table. */
   for (int i = 0; i < n_found; i++) {

      uint32_t idx = patches[i].old_sym_idx;

      if (is64) {
         struct nlist_64 *s = (struct nlist_64 *)(out + symtab_start) + idx;
         s->n_un.n_strx = str_cursor;
      } else {
         struct nlist *s = (struct nlist *)(out + symtab_start) + idx;
         s->n_un.n_strx = str_cursor;
      }

      str_cursor += (uint32_t)(strlen(patches[i].real_name) + 1);
   }

   /*
    * Step 2: Write new undefined nlist entries at insert_idx.
    * These are the new undefined _sym entries.
    */
   for (int i = 0; i < n_found; i++) {

      patches[i].new_sym_idx = insert_idx + (uint32_t)i;

      if (is64) {
         struct nlist_64 e = {0};
         e.n_un.n_strx = str_cursor;
         e.n_type = N_UNDF | N_EXT;
         e.n_sect = NO_SECT;
         e.n_desc = 0;
         e.n_value = 0;
         memcpy(out + old_symtab_end + i * nlist_size, &e, nlist_size);
      } else {
         struct nlist e = {0};
         e.n_un.n_strx = str_cursor;
         e.n_type = N_UNDF | N_EXT;
         e.n_sect = NO_SECT;
         e.n_desc = 0;
         e.n_value = 0;
         memcpy(out + old_symtab_end + i * nlist_size, &e, nlist_size);
      }

      str_cursor += (uint32_t)(strlen(patches[i].orig_name) + 1);
   }

   /* Copy everything between old symtab end and old strtab */
   if (sc->stroff > old_symtab_end) {
      memcpy(out + old_symtab_end + new_nlist_bytes,
             (char *)buf + old_symtab_end,
             sc->stroff - old_symtab_end);
   }

   /* Copy original string table */
   memcpy(out + new_stroff, (char *)buf + sc->stroff, sc->strsize);

   /* Append new strings: first the ___real_* names, then _sym names */
   size_t str_pos = new_stroff + sc->strsize;

   for (int i = 0; i < n_found; i++) {
      size_t len = strlen(patches[i].real_name) + 1;
      memcpy(out + str_pos, patches[i].real_name, len);
      str_pos += len;
   }

   for (int i = 0; i < n_found; i++) {
      size_t len = strlen(patches[i].orig_name) + 1;
      memcpy(out + str_pos, patches[i].orig_name, len);
      str_pos += len;
   }

   /* Copy anything after the original string table */
   size_t old_strtab_end = sc->stroff + sc->strsize;

   if (old_strtab_end < size) {
      memcpy(out + str_pos,
             (char *)buf + old_strtab_end,
             size - old_strtab_end);
   }

   /* Update LC_SYMTAB */
   struct symtab_command *new_sc;
   bool dummy_is64;
   uint32_t dummy_ncmds;
   new_sc = find_symtab_cmd(out, new_size, &dummy_is64, &dummy_ncmds);
   new_sc->nsyms += (uint32_t)n_found;
   new_sc->stroff = new_stroff;
   new_sc->strsize = str_cursor;

   /* Update LC_DYSYMTAB: new entries are undefined externals */
   struct dysymtab_command *new_dsc;
   new_dsc = find_dysymtab_cmd(out, is64, ncmds);

   if (new_dsc)
      new_dsc->nundefsym += (uint32_t)n_found;

   /*
    * Step 3: Patch all relocations that reference old _sym to
    * reference the new undefined _sym instead.
    *
    * We do this on the OUTPUT buffer since all offsets have been
    * copied from the original (relocations are before the symtab
    * so they're at the same file offsets).
    */
   for (int i = 0; i < n_found; i++) {
      patch_relocs(out, new_size, is64, ncmds,
                   patches[i].old_sym_idx,
                   patches[i].new_sym_idx);
   }

   int rc = write_file(path, out, new_size);
   free(out);
   return rc;
}

int
main(int argc, char **argv)
{
   void *buf;
   size_t size;

   if (argc < 3)
      usage(argv[0]);

   buf = read_file(argv[1], &size);

   if (!buf)
      return 1;

   if (strcmp(argv[2], "--list-text-syms") == 0) {
      do_list_text_syms(buf, size);
      free(buf);
      return 0;
   }

   if (strcmp(argv[2], "--patch-syms") == 0) {

      if (argc < 4)
         usage(argv[0]);

      int rc = do_patch_syms(
         argv[1], buf, size, (const char **)&argv[3], argc - 3
      );

      free(buf);
      return rc;
   }

   usage(argv[0]);
   return 1;
}
