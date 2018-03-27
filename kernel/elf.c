
#include <common/string_util.h>

#include <exos/paging.h>
#include <exos/process.h>
#include <exos/kmalloc.h>
#include <exos/fs/exvfs.h>
#include <exos/exos_errno.h>

#include <elf.h>

#ifdef BITS32

static int load_phdr(fs_handle *elf_file,
                     page_directory_t *pdir,
                     Elf32_Phdr *phdr)
{
   ssize_t ret;
   char *vaddr = (char *) (phdr->p_vaddr & PAGE_MASK);

   int page_count = ((phdr->p_memsz + PAGE_SIZE) & PAGE_MASK) >> PAGE_SHIFT;

   {
      uptr end_vaddr = (uptr)vaddr + (page_count << PAGE_SHIFT);

      if (end_vaddr < (phdr->p_vaddr + phdr->p_memsz))
         page_count++;
   }

   // printk("seg at %p, m: %u, p: %i, end: %p, me: %p\n",
   //        phdr->p_vaddr, phdr->p_memsz, page_count,
   //        phdr->p_vaddr + phdr->p_memsz,
   //        (phdr->p_vaddr & PAGE_MASK) + page_count * PAGE_SIZE);

   for (int j = 0; j < page_count; j++, vaddr += PAGE_SIZE) {

      if (is_mapped(pdir, vaddr))
         continue;

      void *p = kzmalloc(PAGE_SIZE);
      VERIFY(p != NULL); // TODO: handle out-of-memory
      uptr paddr = KERNEL_VA_TO_PA(p);

      map_page(pdir, vaddr, paddr, true, true);
   }

   ret = exvfs_seek(elf_file, phdr->p_offset, SEEK_SET);
   VERIFY(ret == (ssize_t)phdr->p_offset);

   ret = exvfs_read(elf_file, (void *) phdr->p_vaddr, phdr->p_filesz);
   VERIFY(ret == (ssize_t)phdr->p_filesz);

   vaddr = (char *) (phdr->p_vaddr & PAGE_MASK);

   /* Make the read-only pages to be read-only */
   for (int j = 0; j < page_count; j++, vaddr += PAGE_SIZE) {
      set_page_rw(pdir, vaddr, !!(phdr->p_flags & PF_W));
   }

   return 0;
}

int load_elf_program(const char *filepath,
                     page_directory_t **pdir_ref,
                     void **entry,
                     void **stack_addr)
{
   Elf32_Phdr *phdrs = NULL;
   Elf32_Ehdr header;
   ssize_t ret;
   int rc = 0;

   fs_handle *elf_file = exvfs_open(filepath);

   if (!elf_file)
      return -ENOENT;

   if (*pdir_ref == NULL)
      *pdir_ref = pdir_clone(get_kernel_page_dir());

   //printk("[kernel] elf loader: '%s'\n", filepath);

   set_page_directory(*pdir_ref);

   ret = exvfs_read(elf_file, &header, sizeof(header));

   if (ret < (int)sizeof(header)) {
      rc = -ENOEXEC;
      goto out;
   }

   if (strncmp((const char *)header.e_ident, ELFMAG, 4)) {
      rc = -ENOEXEC;
      goto out;
   }

   if (header.e_ehsize < sizeof(header)) {
      rc = -ENOEXEC;
      goto out;
   }

   const ssize_t total_phdrs_size = header.e_phnum * sizeof(Elf32_Phdr);
   phdrs = kmalloc(total_phdrs_size);

   if (!phdrs) {
      rc = -ENOMEM;
      goto out;
   }

   ret = exvfs_seek(elf_file, header.e_phoff, SEEK_SET);

   if (ret != (ssize_t)header.e_phoff) {
      rc = -ENOEXEC;
      goto out;
   }

   ret = exvfs_read(elf_file, phdrs, total_phdrs_size);

   if (ret != total_phdrs_size) {
      rc = -ENOEXEC;
      goto out;
   }

   for (int i = 0; i < header.e_phnum; i++) {

      Elf32_Phdr *phdr = phdrs + i;

      if (phdr->p_type == PT_LOAD)
         load_phdr(elf_file, *pdir_ref, phdr);
   }


   // Allocating memory for the user stack.

   const int pages_for_stack = 16;
   void *stack_top =
      (void *) (OFFLIMIT_USERMODE_ADDR - pages_for_stack * PAGE_SIZE);

   for (int i = 0; i < pages_for_stack; i++) {
      void *p = kzmalloc(PAGE_SIZE);
      VERIFY(p != NULL); // TODO: handle out-of-memory
      uptr paddr = KERNEL_VA_TO_PA(p);
      map_page(*pdir_ref, stack_top + i * PAGE_SIZE, paddr, true, true);
   }

   // Finally setting the output-params.

   *stack_addr = (void *) ((OFFLIMIT_USERMODE_ADDR - 1) & ~15);
   *entry = (void *) header.e_entry;

out:
   exvfs_close(elf_file);
   kfree(phdrs);
   return rc;
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
