
#include <common/string_util.h>

#include <exos/paging.h>
#include <exos/process.h>
#include <exos/kmalloc.h>
#include <exos/fs/exvfs.h>
#include <exos/exos_errno.h>

#include <elf.h>

#ifdef BITS32

int load_elf_program(const char *filepath,
                     page_directory_t **pdir_ref,
                     void **entry,
                     void **stack_addr)
{
   ssize_t ret;
   Elf32_Ehdr header;

   fs_handle *elf_file = exvfs_open(filepath);

   if (!elf_file) {
      return -ENOENT;
   }

   if (*pdir_ref == NULL) {
      *pdir_ref = pdir_clone(get_kernel_page_dir());
   }

   set_page_directory(*pdir_ref);

   ret = exvfs_read(elf_file, &header, sizeof(header));
   ASSERT(ret == sizeof(header));

   ASSERT(header.e_ident[EI_MAG0] == ELFMAG0);
   ASSERT(header.e_ident[EI_MAG1] == ELFMAG1);
   ASSERT(header.e_ident[EI_MAG2] == ELFMAG2);
   ASSERT(header.e_ident[EI_MAG3] == ELFMAG3);

   ASSERT(header.e_ehsize == sizeof(header));

   const ssize_t total_phdrs_size = header.e_phnum * sizeof(Elf32_Phdr);
   Elf32_Phdr *phdrs = kmalloc(total_phdrs_size);
   VERIFY(phdrs != NULL);

   ret = exvfs_seek(elf_file, header.e_phoff, SEEK_SET);
   VERIFY(ret == (ssize_t)header.e_phoff);

   ret = exvfs_read(elf_file, phdrs, total_phdrs_size);
   ASSERT(ret == total_phdrs_size);

   for (int i = 0; i < header.e_phnum; i++) {

      Elf32_Phdr *phdr = phdrs + i;

      // Ignore non-load segments.
      if (phdr->p_type != PT_LOAD) {
         continue;
      }

      int pages_count =
         ((phdr->p_memsz + PAGE_SIZE) & PAGE_MASK) >> PAGE_SHIFT;

      if ((phdr->p_memsz < PAGE_SIZE) &&
          ((phdr->p_vaddr + phdr->p_memsz) & PAGE_MASK) >
          (phdr->p_vaddr & PAGE_MASK)) {

         // Cross-page small segment
         pages_count++;
      }

      char *vaddr = (char *) (phdr->p_vaddr & PAGE_MASK);

      for (int j = 0; j < pages_count; j++, vaddr += PAGE_SIZE) {

         if (is_mapped(*pdir_ref, vaddr)) {
            continue;
         }

         void *p = kmalloc(PAGE_SIZE);
         VERIFY(p != NULL); // TODO: handle out-of-memory
         uptr paddr = KERNEL_VA_TO_PA(p);

         map_page(*pdir_ref, vaddr, paddr, true, true);
         bzero(vaddr, PAGE_SIZE);
      }

      ret = exvfs_seek(elf_file, phdr->p_offset, SEEK_SET);
      VERIFY(ret == (ssize_t)phdr->p_offset);

      ret = exvfs_read(elf_file, (void *) phdr->p_vaddr, phdr->p_filesz);
      VERIFY(ret == (ssize_t)phdr->p_filesz);

      vaddr = (char *) (phdr->p_vaddr & PAGE_MASK);
      for (int j = 0; j < pages_count; j++, vaddr += PAGE_SIZE) {
         set_page_rw(*pdir_ref, vaddr, !!(phdr->p_flags & PF_W));
      }
   }

   exvfs_close(elf_file);

   // Allocating memory for the user stack.

   const int pages_for_stack = 16;
   void *stack_top =
      (void *) (OFFLIMIT_USERMODE_ADDR - pages_for_stack * PAGE_SIZE);

   for (int i = 0; i < pages_for_stack; i++) {
      void *p = kmalloc(PAGE_SIZE);
      VERIFY(p != NULL); // TODO: handle out-of-memory
      uptr paddr = KERNEL_VA_TO_PA(p);
      map_page(*pdir_ref, stack_top + i * PAGE_SIZE, paddr, true, true);
   }

   // Finally setting the output-params.

   *stack_addr = (void *) ((OFFLIMIT_USERMODE_ADDR - 1) & ~15);
   *entry = (void *) header.e_entry;

   kfree2(phdrs, total_phdrs_size);
   return 0;
}

/* ------------------- Debug utils ---------------------- */

void get_symtab_and_strtab(Elf32_Shdr **symtab, Elf32_Shdr **strtab)
{
   Elf32_Ehdr *h = (Elf32_Ehdr*)(KERNEL_PA_TO_VA(KERNEL_PADDR));
   VERIFY(h->e_shentsize == sizeof(Elf32_Shdr));

   *symtab = NULL;
   *strtab = NULL;

   Elf32_Shdr *sections = (Elf32_Shdr *) ((char *)h + h->e_shoff);

   for (u32 i = 0; i < h->e_shnum; i++) {
      Elf32_Shdr *s = sections + i;

      if (s->sh_type == SHT_SYMTAB) {
         ASSERT(!*symtab);
         *symtab = s;
      } else if (s->sh_type == SHT_STRTAB && i != h->e_shstrndx) {
         ASSERT(!*strtab);
         *strtab = s;
      }
   }

   VERIFY(*symtab != NULL);
   VERIFY(*strtab != NULL);
}

const char *find_sym_at_addr(uptr vaddr, ptrdiff_t *offset)
{
   Elf32_Shdr *symtab;
   Elf32_Shdr *strtab;

   get_symtab_and_strtab(&symtab, &strtab);

   Elf32_Sym *syms = (Elf32_Sym *) symtab->sh_addr;
   const int sym_count = symtab->sh_size / sizeof(Elf32_Sym);

   for (int i = 0; i < sym_count; i++) {
      Elf32_Sym *s = syms + i;
      if (s->st_value < vaddr && vaddr <= s->st_value + s->st_size) {
         *offset = vaddr - s->st_value;
         return (char *)strtab->sh_addr + s->st_name;
      }
   }

   return NULL;
}

uptr find_addr_of_symbol(const char *searched_sym)
{
   Elf32_Shdr *symtab;
   Elf32_Shdr *strtab;

   get_symtab_and_strtab(&symtab, &strtab);

   Elf32_Sym *syms = (Elf32_Sym *) symtab->sh_addr;
   const int sym_count = symtab->sh_size / sizeof(Elf32_Sym);

   for (int i = 0; i < sym_count; i++) {
      if (!strcmp((char *)strtab->sh_addr + syms[i].st_name, searched_sym))
         return syms[i].st_value;
   }

   return 0;
}

#endif // BITS32
