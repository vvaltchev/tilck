/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/paging.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/fault_resumable.h>

static int load_phdr(fs_handle *elf_file,
                     page_directory_t *pdir,
                     Elf_Phdr *phdr,
                     uptr *end_vaddr_ref)
{
   int rc;
   ssize_t ret;
   char *vaddr = (char *) (phdr->p_vaddr & PAGE_MASK);

   if (phdr->p_memsz == 0)
      return 0; /* very weird (because the phdr has type LOAD) */

   uptr sz = phdr->p_vaddr + phdr->p_memsz - (uptr)vaddr;
   int page_count = (sz + PAGE_SIZE - 1) / PAGE_SIZE;
   *end_vaddr_ref = (uptr)vaddr + (page_count << PAGE_SHIFT);


   for (int j = 0; j < page_count; j++, vaddr += PAGE_SIZE) {

      if (is_mapped(pdir, vaddr))
         continue;

      void *p = kzmalloc(PAGE_SIZE);

      if (!p)
         return -ENOMEM;

      rc = map_page(pdir, vaddr, KERNEL_VA_TO_PA(p), true, true);

      if (rc != 0)
         return rc;
   }

   ret = vfs_seek(elf_file, phdr->p_offset, SEEK_SET);

   if (ret != (ssize_t)phdr->p_offset)
      return -ENOEXEC;

   ret = vfs_read(elf_file, (void *) phdr->p_vaddr, phdr->p_filesz);

   if (ret != (ssize_t)phdr->p_filesz)
      return -ENOEXEC;

   return 0;
}

static void
phdr_adjust_page_access(page_directory_t *pdir, Elf_Phdr *phdr)
{
   char *vaddr = (char *) (phdr->p_vaddr & PAGE_MASK);

   if (phdr->p_memsz == 0)
      return; /* very weird (because the phdr has type LOAD) */

   uptr sz = phdr->p_vaddr + phdr->p_memsz - (uptr)vaddr;
   int page_count = (sz + PAGE_SIZE - 1) / PAGE_SIZE;

   /* Make the read-only pages to be read-only */
   for (int j = 0; j < page_count; j++, vaddr += PAGE_SIZE)
      if (!(phdr->p_flags & PF_W))
         set_page_rw(pdir, vaddr, false);
}

int load_elf_program(const char *filepath,
                     page_directory_t **pdir_ref,
                     void **entry,
                     void **stack_addr,
                     void **brk_ref)
{
   page_directory_t *old_pdir = get_curr_pdir();
   Elf_Phdr *phdrs = NULL;
   ssize_t total_phdrs_size = 0;
   fs_handle elf_file = NULL;
   Elf_Ehdr header;
   ssize_t ret;
   uptr brk = 0;
   int rc = 0;

   ASSERT(!is_preemption_enabled());
   enable_preemption();
   {
      rc = vfs_open(filepath, &elf_file);
   }
   disable_preemption();

   if (rc < 0)
      return rc;

   ASSERT(elf_file != NULL);

   if (*pdir_ref == NULL) {

      *pdir_ref = pdir_clone(get_kernel_pdir());

      if (!*pdir_ref) {
         vfs_close(elf_file);
         return -ENOMEM;
      }
   }

   //printk("elf loader: '%s'\n", filepath);

   set_page_directory(*pdir_ref);

   ret = vfs_read(elf_file, &header, sizeof(header));

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

   total_phdrs_size = header.e_phnum * sizeof(Elf_Phdr);
   phdrs = kmalloc(total_phdrs_size);

   if (!phdrs) {
      rc = -ENOMEM;
      goto out;
   }

   ret = vfs_seek(elf_file, header.e_phoff, SEEK_SET);

   if (ret != (ssize_t)header.e_phoff) {
      rc = -ENOEXEC;
      goto out;
   }

   ret = vfs_read(elf_file, phdrs, total_phdrs_size);

   if (ret != total_phdrs_size) {
      rc = -ENOEXEC;
      goto out;
   }

   for (int i = 0; i < header.e_phnum; i++) {

      uptr end_vaddr = 0;
      Elf_Phdr *phdr = phdrs + i;

      if (phdr->p_type != PT_LOAD)
         continue;

      rc = load_phdr(elf_file, *pdir_ref, phdr, &end_vaddr);

      if (rc < 0)
         goto out;

      if (end_vaddr > brk)
         brk = end_vaddr;
   }

   for (int i = 0; i < header.e_phnum; i++) {

      Elf_Phdr *phdr = phdrs + i;

      if (phdr->p_type == PT_LOAD)
         phdr_adjust_page_access(*pdir_ref, phdr);
   }

   // Allocating memory for the user stack.

   const int pages_for_stack = USER_STACK_PAGES;
   const uptr stack_top = (USERMODE_VADDR_END - USER_STACK_PAGES * PAGE_SIZE);

   for (int i = 0; i < pages_for_stack; i++) {

      void *p = kzmalloc(PAGE_SIZE);

      if (!p) {
         rc = -ENOMEM;
         goto out;
      }

      int rc = map_page(*pdir_ref,
                        (void *)stack_top + (i << PAGE_SHIFT),
                        KERNEL_VA_TO_PA(p),
                        true,
                        true);

      if (rc != 0)
         goto out;
   }

   // Finally setting the output-params.

   *stack_addr = (void *) ((USERMODE_VADDR_END - 1) & POINTER_ALIGN_MASK);
   *entry = (void *) header.e_entry;
   *brk_ref = (void *)brk;

out:
   vfs_close(elf_file);
   kfree2(phdrs, total_phdrs_size);

   if (rc != 0) {
      set_page_directory(old_pdir);
      pdir_destroy(*pdir_ref);
      *pdir_ref = NULL;
   }

   return rc;
}

void get_symtab_and_strtab(Elf_Shdr **symtab, Elf_Shdr **strtab)
{
   Elf_Ehdr *h = (Elf_Ehdr*)(KERNEL_PA_TO_VA(KERNEL_PADDR));
   VERIFY(h->e_shentsize == sizeof(Elf_Shdr));

   *symtab = NULL;
   *strtab = NULL;

   Elf_Shdr *sections = (Elf_Shdr *) ((char *)h + h->e_shoff);

   for (u32 i = 0; i < h->e_shnum; i++) {
      Elf_Shdr *s = sections + i;

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
   Elf_Shdr *symtab;
   Elf_Shdr *strtab;

   get_symtab_and_strtab(&symtab, &strtab);

   Elf_Sym *syms = (Elf_Sym *) symtab->sh_addr;
   const int sym_count = symtab->sh_size / sizeof(Elf_Sym);

   for (int i = 0; i < sym_count; i++) {
      Elf_Sym *s = syms + i;

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
   Elf_Shdr *symtab;
   Elf_Shdr *strtab;

   get_symtab_and_strtab(&symtab, &strtab);

   Elf_Sym *syms = (Elf_Sym *) symtab->sh_addr;
   const int sym_count = symtab->sh_size / sizeof(Elf_Sym);

   for (int i = 0; i < sym_count; i++) {
      if (!strcmp((char *)strtab->sh_addr + syms[i].st_name, searched_sym))
         return syms[i].st_value;
   }

   return 0;
}

static void
find_sym_at_addr_no_ret(uptr vaddr,
                        ptrdiff_t *offset,
                        u32 *sym_size,
                        const char **sym_name_ref)
{
  *sym_name_ref = find_sym_at_addr(vaddr, offset, sym_size);
}

const char *
find_sym_at_addr_safe(uptr vaddr, ptrdiff_t *offset, u32 *sym_size)
{
   const char *sym_name = NULL;
   fault_resumable_call(~0, &find_sym_at_addr_no_ret, 4,
                        vaddr, offset, sym_size, &sym_name);

   return sym_name;
}

