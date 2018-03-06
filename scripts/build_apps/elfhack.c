
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
}

void show_help(char **argv)
{
   fprintf(stderr, "Usage: %s <elf_file> [--dump <section name>]\n", argv[0]);
   fprintf(stderr, "       %s <elf_file> [--move_metadata]\n", argv[0]);
   fprintf(stderr, "       %s <elf_file> [--copy <src section> <dest section>]\n", argv[0]);
   fprintf(stderr, "       %s <elf_file> [--rename <section> <new_name>]\n", argv[0]);
   fprintf(stderr, "       %s <elf_file> [--link <section> <linked_section>]\n", argv[0]);
   exit(1);
}

void rename_section(void *mapped_elf_file,
                    const char *sec, const char *new_name)
{
   Elf32_Ehdr *h = (Elf32_Ehdr*)mapped_elf_file;
   char *hc = (char *)h;
   Elf32_Shdr *sections = (Elf32_Shdr *)(hc + h->e_shoff);
   Elf32_Shdr *shstrtab = sections + h->e_shstrndx;

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

   fd = open(file, O_RDWR);

   if (fd < 0) {
      perror(NULL);
      return 1;
   }

   errno = 0;

   vaddr = mmap(NULL,                   /* addr */
                1024 * 1024,            /* length */
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
   } else {
      show_help(argv);
   }

   ret = munmap(vaddr, 1024 * 1024);

   if (ret < 0) {
      perror(NULL);
   }

   close(fd);
   return 0;
}
