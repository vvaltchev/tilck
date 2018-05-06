
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <elf.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MMAP_SIZE (1024*1024)

Elf32_Shdr *get_section(void *mapped_elf_file, const char *section_name)
{
   Elf32_Ehdr *h = (Elf32_Ehdr*)mapped_elf_file;
   Elf32_Shdr *sections = (Elf32_Shdr *) ((char *)h + h->e_shoff);
   Elf32_Shdr *section_header_strtab = sections + h->e_shstrndx;

   for (uint32_t i = 0; i < h->e_shnum; i++) {

      Elf32_Shdr *s = sections + i;
      char *name = (char *)h + section_header_strtab->sh_offset + s->sh_name;

      if (!strcmp(name, section_name)) {
         return s;
      }
   }

   fprintf(stderr, "No section '%s'\n", section_name);
   exit(1);
}

void section_dump(void *mapped_elf_file, const char *section_name)
{
   Elf32_Ehdr *h = (Elf32_Ehdr*)mapped_elf_file;
   Elf32_Shdr *s = get_section(mapped_elf_file, section_name);
   fwrite((char*)h + s->sh_offset, 1, s->sh_size, stdout);
}

void copy_section(void *mapped_elf_file, const char *src, const char *dst)
{
   Elf32_Ehdr *h = (Elf32_Ehdr*)mapped_elf_file;
   Elf32_Shdr *s_src = get_section(mapped_elf_file, src);
   Elf32_Shdr *s_dst = get_section(mapped_elf_file, dst);

   if (!dst) {
      fprintf(stderr, "Missing <dst section> argument\n");
      exit(1);
   }

   if (s_src->sh_size > s_dst->sh_size) {
      fprintf(stderr, "The source section '%s' is too big "
              "[%u bytes] to fit in the dest section '%s' [%u bytes]\n",
              src, s_src->sh_size, dst, s_dst->sh_size);
      exit(1);
   }

   memcpy((char*)h + s_dst->sh_offset,
          (char*)h + s_src->sh_offset,
          s_src->sh_size);

   s_dst->sh_info = s_src->sh_info;
   s_dst->sh_flags = s_src->sh_flags;
   s_dst->sh_type = s_src->sh_type;
   s_dst->sh_entsize = s_src->sh_entsize;
   s_dst->sh_size = s_src->sh_size;
}

void show_help(char **argv)
{
   fprintf(stderr, "Usage: %s <elf_file> [--dump <section name>]\n", argv[0]);
   fprintf(stderr, "       %s <elf_file> [--move_metadata]\n", argv[0]);
   fprintf(stderr, "       %s <elf_file> [--copy <src section> <dest section>]\n", argv[0]);
   fprintf(stderr, "       %s <elf_file> [--rename <section> <new_name>]\n", argv[0]);
   fprintf(stderr, "       %s <elf_file> [--link <section> <linked_section>]\n", argv[0]);
   fprintf(stderr, "       %s <elf_file> [--drop-last-section]\n", argv[0]);
   fprintf(stderr, "       %s <elf_file> [--set-phdr-rwx-flags <phdr index> <rwx flags>]\n", argv[0]);
   exit(1);
}

void rename_section(void *mapped_elf_file,
                    const char *sec, const char *new_name)
{
   Elf32_Ehdr *h = (Elf32_Ehdr*)mapped_elf_file;
   char *hc = (char *)h;
   Elf32_Shdr *sections = (Elf32_Shdr *)(hc + h->e_shoff);
   Elf32_Shdr *shstrtab = sections + h->e_shstrndx;

   if (!new_name) {
      fprintf(stderr, "Missing <new_name> argument\n");
      exit(1);
   }

   if (strlen(new_name) > strlen(sec)) {
      fprintf(stderr, "Section rename with length > old one NOT supported.\n");
      exit(1);
   }

   Elf32_Shdr *s = get_section(mapped_elf_file, sec);
   strcpy(hc + shstrtab->sh_offset + s->sh_name, new_name);
}

void link_sections(void *mapped_elf_file,
                   const char *sec, const char *linked)
{
   Elf32_Ehdr *h = (Elf32_Ehdr*)mapped_elf_file;
   char *hc = (char *)h;
   Elf32_Shdr *sections = (Elf32_Shdr *)(hc + h->e_shoff);
   Elf32_Shdr *shstrtab = sections + h->e_shstrndx;

   if (!linked) {
      fprintf(stderr, "Missing <linked section> argument\n");
      exit(1);
   }

   Elf32_Shdr *a = get_section(mapped_elf_file, sec);
   Elf32_Shdr *b = get_section(mapped_elf_file, linked);

   unsigned bidx = (b - sections);
   a->sh_link = bidx;
}

void move_metadata(void *mapped_elf_file)
{
   Elf32_Ehdr *h = (Elf32_Ehdr*)mapped_elf_file;
   char *hc = (char *)h;

   size_t off = h->e_ehsize;

   memcpy(hc + off, hc + h->e_phoff, h->e_phentsize*h->e_phnum);
   h->e_phoff = off;
   off += h->e_phentsize*h->e_phnum;

   memcpy(hc + off, hc + h->e_shoff, h->e_shentsize*h->e_shnum);
   h->e_shoff = off;
   off += h->e_shentsize*h->e_shnum;

   Elf32_Shdr *sections = (Elf32_Shdr *) (hc + h->e_shoff);
   Elf32_Shdr *shstrtab = sections + h->e_shstrndx;

   memcpy(hc + off, hc + shstrtab->sh_offset, shstrtab->sh_size);
   shstrtab->sh_offset = off;

   Elf32_Phdr *phdrs = (Elf32_Phdr *)(hc + h->e_phoff);
   shstrtab->sh_addr = phdrs[0].p_vaddr + shstrtab->sh_offset;
   shstrtab->sh_flags |= SHF_ALLOC;

   for (uint32_t i = 0; i < h->e_shnum; i++) {
      Elf32_Shdr *s = sections + i;

      /* Make sure that all the sections with a vaddr != 0 are 'alloc' */
      if (s->sh_addr)
         s->sh_flags |= SHF_ALLOC;
   }
}

void drop_last_section(void **mapped_elf_file_ref, int fd)
{
   Elf32_Ehdr *h = (Elf32_Ehdr*)*mapped_elf_file_ref;
   char *hc = (char *)h;
   Elf32_Shdr *sections = (Elf32_Shdr *)(hc + h->e_shoff);
   Elf32_Shdr *shstrtab = sections + h->e_shstrndx;

   Elf32_Shdr *last_section = NULL;
   int last_section_index = 0;
   off_t last_offset = 0;

   for (uint32_t i = 0; i < h->e_shnum; i++) {

      Elf32_Shdr *s = sections + i;

      if (s->sh_offset > last_offset) {
         last_section = s;
         last_offset = s->sh_offset;
         last_section_index = i;
      }
   }

   if (last_section == shstrtab) {
      fprintf(stderr, "The last section is .shstrtab and it cannot be removed!\n");
      exit(1);
   }

   if (last_section_index != h->e_shnum - 1) {

      /*
       * If the last section physically on file is not the last section in
       * the table, we cannot just decrease h->e_shnum, otherwise we'll remove
       * from the table an useful section. Therefore, in that case we just
       * use the slot of the last_section to store the section metainfo of the
       * section with the biggest index in the section table (last section in
       * another sense).
       */

      *last_section = sections[h->e_shnum - 1];

      /*
       * If we're so unlucky that the section with the biggest index in the
       * section table is also the special .shstrtab, we have to update its
       * index in the ELF header as well.
       */
      if (h->e_shstrndx == h->e_shnum - 1) {
         h->e_shstrndx = last_section_index;
      }
   }

   /* Drop the last section from the section table */
   h->e_shnum--;

   /*
    * Unlink all the sections depending on this one. Yes, this is rough,
    * but it's fine. Users of this script MUST know exactly what they're doing.
    * In particular, for the main use of this feature (drop of the old symtab
    * and strtab), it is expected this function to be just used twice.
    */
   for (uint32_t i = 0; i < h->e_shnum; i++)
      if (sections[i].sh_link == last_section_index)
         sections[i].sh_link = 0;

   /*
    * Unfortunately, the "bash for Windows" subsystem does not support
    * ftruncate on memory-mapped files. Even if having the exOS to work there
    * is _not_ a must (users are supposed to use Linux), it is a nice-to-have
    * feature. Therefore, here we first unmap the memory-mapped ELF file and
    * then we truncate it.
    */
   if (munmap(*mapped_elf_file_ref, MMAP_SIZE) < 0) {
      perror("munmap() failed");
      exit(1);
   }

   *mapped_elf_file_ref = NULL;

   /* Physically remove the last section from the file, by truncating it */
   if (ftruncate(fd, last_offset) < 0) {

      fprintf(stderr, "ftruncate(%i, %li) failed with '%s'\n",
              fd, last_offset, strerror(errno));
      exit(1);
   }
}

void set_phdr_rwx_flags(void *mapped_elf_file,
                        const char *phdr_index,
                        const char *flags)
{
   Elf32_Ehdr *h = (Elf32_Ehdr*)mapped_elf_file;
   errno = 0;

   char *endptr = NULL;
   unsigned long phindex = strtoul(phdr_index, &endptr, 10);

   if (errno || *endptr != '\0') {
      fprintf(stderr, "Invalid phdr index '%s'\n", phdr_index);
      exit(1);
   }

   if (phindex >= h->e_phnum) {
      fprintf(stderr, "Phdr index %lu out-of-range [0, %u].\n",
              phindex, h->e_phnum - 1);
      exit(1);
   }

   if (!flags) {
      fprintf(stderr, "Missing <rwx flags> argument.\n");
      exit(1);
   }

   char *hc = (char *)h;
   Elf32_Phdr *phdrs = (Elf32_Phdr *)(hc + h->e_phoff);
   Elf32_Phdr *phdr = phdrs + phindex;

   unsigned f = 0;

   while (*flags) {
      switch (*flags) {
         case 'r':
            f |= PF_R;
            break;
         case 'w':
            f |= PF_W;
            break;
         case 'x':
            f |= PF_X;
            break;
         default:
            fprintf(stderr, "Invalid flag '%c'. Allowed: r,w,x.\n", *flags);
            exit(1);
      }
      flags++;
   }

   // First, clear the already set RWX flags (be keep the others!)
   phdr->p_flags &= ~(PF_R | PF_W | PF_X);

   // Then, set the new RWX flags.
   phdr->p_flags |= f;
}

int main(int argc, char **argv)
{
   void *vaddr;
   int ret;
   int fd;

   if (argc < 3) {
      show_help(argv);
   }

   const char *file = argv[1];
   const char *opt = argv[2];
   const char *opt_arg = argv[3];
   const char *opt_arg2 = argv[4];

   if (argc == 3)
      opt_arg2 = NULL;

   fd = open(file, O_RDWR);

   if (fd < 0) {
      perror(NULL);
      return 1;
   }

   errno = 0;

   vaddr = mmap(NULL,                   /* addr */
                MMAP_SIZE,              /* length */
                PROT_READ | PROT_WRITE, /* prot */
                MAP_SHARED,             /* flags */
                fd,                     /* fd */
                0);                     /* offset */

   if (errno) {
      perror(NULL);
      return 1;
   }

   if (!strcmp(opt, "--dump")) {
      section_dump(vaddr, opt_arg);
   } else if (!strcmp(opt, "--move_metadata")) {
      move_metadata(vaddr);
   } else if (!strcmp(opt, "--copy")) {
      copy_section(vaddr, opt_arg, opt_arg2);
   } else if (!strcmp(opt, "--rename")) {
      rename_section(vaddr, opt_arg, opt_arg2);
   } else if (!strcmp(opt, "--link")) {
      link_sections(vaddr, opt_arg, opt_arg2);
   } else if (!strcmp(opt, "--drop-last-section")) {
      drop_last_section(&vaddr, fd);
   } else if (!strcmp(opt, "--set-phdr-rwx-flags")) {
      set_phdr_rwx_flags(vaddr, opt_arg, opt_arg2);
   } else {
      show_help(argv);
   }

   /*
    * Do munmap() only if vaddr != NULL.
    * Reason: some functions (at the moment only drop_last_section()) may
    * have their reasons for calling munmap() earlier. Do avoid double-calling
    * it and getting an error, such functions will just set vaddr to NULL.
    */
   if (vaddr && munmap(vaddr, MMAP_SIZE) < 0) {
      perror("munmap() failed");
   }

   close(fd);
   return 0;
}
