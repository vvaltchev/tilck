
#include <common/string_util.h>

#include <exos/paging.h>
#include <exos/process.h>
#include <exos/kmalloc.h>
#include <exos/fs/exvfs.h>
#include <exos/errno.h>

#include <elf.h>

#ifdef BITS32

static int load_phdr(fs_handle *elf_file,
                     page_directory_t *pdir,
                     Elf32_Phdr *phdr,
                     uptr *end_vaddr_ref)
{
   ssize_t ret;
   char *vaddr = (char *) (phdr->p_vaddr & PAGE_MASK);

   if (phdr->p_memsz == 0)
      return 0; /* very weird (because the phdr has type LOAD) */

   int page_count = ((phdr->p_memsz + PAGE_SIZE) & PAGE_MASK) >> PAGE_SHIFT;

   *end_vaddr_ref = (uptr)vaddr + (page_count << PAGE_SHIFT);

   if (*end_vaddr_ref < (phdr->p_vaddr + phdr->p_memsz)) {
      page_count++;
      *end_vaddr_ref += PAGE_SIZE;
   }

   for (int j = 0; j < page_count; j++, vaddr += PAGE_SIZE) {

      if (is_mapped(pdir, vaddr))
         continue;

      void *p = kzmalloc(PAGE_SIZE);

      if (!p)
         return -ENOMEM;

      map_page(pdir, vaddr, KERNEL_VA_TO_PA(p), true, true);
   }

   ret = exvfs_seek(elf_file, phdr->p_offset, SEEK_SET);

   if (ret != (ssize_t)phdr->p_offset)
      return -ENOEXEC;

   ret = exvfs_read(elf_file, (void *) phdr->p_vaddr, phdr->p_filesz);

   if (ret != (ssize_t)phdr->p_filesz)
      return -ENOEXEC;

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
                     void **stack_addr,
                     void **brk_ref)
{
   page_directory_t *old_pdir = get_curr_page_dir();
   Elf32_Phdr *phdrs = NULL;
   fs_handle elf_file = NULL;
   Elf32_Ehdr header;
   ssize_t ret;
   uptr brk = 0;
   int rc = 0;

   ASSERT(!is_preemption_enabled());
   enable_preemption();
   {
      rc = exvfs_open(filepath, &elf_file);
   }
   disable_preemption();

   if (rc < 0)
      return rc;

   ASSERT(elf_file != NULL);

   if (*pdir_ref == NULL) {

      *pdir_ref = pdir_clone(get_kernel_page_dir());

      if (!*pdir_ref) {
         exvfs_close(elf_file);
         return -ENOMEM;
      }
   }

   //printk("elf loader: '%s'\n", filepath);

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

      u32 end_vaddr = 0;
      Elf32_Phdr *phdr = phdrs + i;

      if (phdr->p_type != PT_LOAD)
         continue;

      rc = load_phdr(elf_file, *pdir_ref, phdr, &end_vaddr);

      if (rc < 0)
         goto out;

      if (end_vaddr > brk)
         brk = end_vaddr;
   }


   // Allocating memory for the user stack.

   const int pages_for_stack = 16;
   void *stack_top =
      (void *) (OFFLIMIT_USERMODE_ADDR - pages_for_stack * PAGE_SIZE);

   for (int i = 0; i < pages_for_stack; i++) {

      void *p = kzmalloc(PAGE_SIZE);

      if (!p) {
         rc = -ENOMEM;
         goto out;
      }

      map_page(*pdir_ref,
               stack_top + i * PAGE_SIZE,
               KERNEL_VA_TO_PA(p),
               true,
               true);
   }

   // Finally setting the output-params.

   *stack_addr = (void *) ((OFFLIMIT_USERMODE_ADDR - 1) & POINTER_ALIGN_MASK);
   *entry = (void *) header.e_entry;
   *brk_ref = (void *)brk;

out:
   exvfs_close(elf_file);
   kfree(phdrs);

   if (rc != 0) {
      set_page_directory(old_pdir);
      pdir_destroy(*pdir_ref);
      *pdir_ref = NULL;
   }

   return rc;
}

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

const char *find_sym_at_addr(uptr vaddr, ptrdiff_t *offset, u32 *sym_size)
{
   Elf32_Shdr *symtab;
   Elf32_Shdr *strtab;

   get_symtab_and_strtab(&symtab, &strtab);

   Elf32_Sym *syms = (Elf32_Sym *) symtab->sh_addr;
   const int sym_count = symtab->sh_size / sizeof(Elf32_Sym);

   for (int i = 0; i < sym_count; i++) {
      Elf32_Sym *s = syms + i;

      if (s->st_value <= vaddr && vaddr < s->st_value + s->st_size) {

         *offset = vaddr - s->st_value;

         if (sym_size)
            *sym_size = s->st_size;

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
