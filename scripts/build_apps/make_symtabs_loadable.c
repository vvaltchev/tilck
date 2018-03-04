
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>

#include <elf.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

void dump_syms(void *mapped_elf_file)
{
   Elf32_Ehdr *h = (Elf32_Ehdr*)mapped_elf_file;
   assert(h->e_shentsize == sizeof(Elf32_Shdr));

   Elf32_Shdr *sections = (Elf32_Shdr *) ((char *)h + h->e_shoff);
   Elf32_Shdr *section_header_strtab = sections + h->e_shstrndx;

   Elf32_Shdr *symtab = NULL;
   Elf32_Shdr *strtab = NULL;

   for (uint32_t i = 0; i < h->e_shnum; i++) {
      Elf32_Shdr *s = sections + i;
      char *name = (char *)h + section_header_strtab->sh_offset + s->sh_name;
      printf("section: '%s', vaddr: %p, size: %u\n",
             name, (void*)(size_t)s->sh_addr, s->sh_size);

      if (s->sh_type == SHT_SYMTAB) {
         symtab = s;
      } else if (s->sh_type == SHT_STRTAB && i != h->e_shstrndx) {
         strtab = s;
      }
   }

   printf("Symbols:\n");
   Elf32_Sym *syms = (Elf32_Sym *) ((char *)h + symtab->sh_offset);

   for (uint32_t i = 0; i < 10; i++) {
      Elf32_Sym *s = syms + i;
      char *name = (char *)h + strtab->sh_offset + s->st_name;
      printf("%p: %s\n", (void*)(size_t)s->st_value, name);
   }
}

// Returns (largest section offset) + its size
size_t find_last_section_end(void *mapped_elf_file)
{
   Elf32_Ehdr *h = (Elf32_Ehdr*)mapped_elf_file;
   Elf32_Shdr *sections = (Elf32_Shdr *) ((char *)h + h->e_shoff);

   size_t end = 0;

   for (uint32_t i = 0; i < h->e_shnum; i++) {
      Elf32_Shdr *s = sections + i;

      if (s->sh_type == SHT_NOBITS)
         continue;

      //printf("[%u] %x + %x\n", i, s->sh_offset, s->sh_size);

      if (s->sh_offset + s->sh_size > end) {
         end = s->sh_offset + s->sh_size;
         //printf("update end to: %x\n", end);
      }
   }

   return end;
}

int main(int argc, char **argv)
{
   void *vaddr;
   int ret;
   int fd;

   if (argc < 2) {
      fprintf(stderr, "Usage: %s <elf_file>\n", argv[0]);
      return 1;
   }

   fd = open(argv[1], O_RDWR);

   if (fd < 0) {
      perror(NULL);
      return 1;
   }

   vaddr = mmap(NULL,                   /* addr */
                1024 * 1024,            /* length */
                PROT_READ | PROT_WRITE, /* prot */
                MAP_PRIVATE,            /* flags */
                fd,                     /* fd */
                0);                     /* offset */

   //dump_syms(vaddr);

   size_t real_end = find_last_section_end(vaddr);
   printf("end offset: %p\n", (void *)real_end);


   ret = munmap(vaddr, 1024 * 1024);

   if (ret < 0) {
      perror(NULL);
   }

   close(fd);
   return 0;
}
