
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

void section_dump(void *mapped_elf_file, const char *section_name)
{
   Elf32_Ehdr *h = (Elf32_Ehdr*)mapped_elf_file;
   Elf32_Shdr *sections = (Elf32_Shdr *) ((char *)h + h->e_shoff);
   Elf32_Shdr *section_header_strtab = sections + h->e_shstrndx;

   for (uint32_t i = 0; i < h->e_shnum; i++) {

      Elf32_Shdr *s = sections + i;
      char *name = (char *)h + section_header_strtab->sh_offset + s->sh_name;

      if (!strcmp(name, section_name)) {
         fwrite((char*)h + s->sh_offset, 1, s->sh_size, stdout);
         return;
      }
   }
}

void show_help(char **argv)
{
   fprintf(stderr, "Usage: %s <elf_file> [-d <section name>]\n", argv[0]);
   fprintf(stderr, "       %s <elf_file> [--move_metadata]\n", argv[0]);
   exit(1);
}

void move_metadata(void *mapped_elf_file)
{
   Elf32_Ehdr *h = (Elf32_Ehdr*)mapped_elf_file;

   size_t off = h->e_ehsize;

   printf("phdrs at: %u, size: %u\n", h->e_phoff, h->e_phentsize*h->e_phnum);
   printf("sh at: %u, size: %u\n", h->e_shoff, h->e_shentsize*h->e_shnum);

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

   if (!strcmp(opt, "-d")) {
      section_dump(vaddr, opt_arg);
   } else if (!strcmp(opt, "--move_metadata")) {
      move_metadata(vaddr);
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
