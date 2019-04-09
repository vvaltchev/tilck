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
                     pdir_t *pdir,
                     Elf_Phdr *phdr,
                     uptr *end_vaddr_ref)
{
   int rc;
   ssize_t ret;
   char *vaddr = (char *) (phdr->p_vaddr & PAGE_MASK);
   uptr va = phdr->p_vaddr;

   if (phdr->p_memsz == 0)
      return 0; /* very weird (because the phdr has type LOAD) */

   size_t memsz = phdr->p_vaddr + phdr->p_memsz - (uptr)vaddr;
   size_t page_count = (memsz + PAGE_SIZE - 1) / PAGE_SIZE;
   size_t filesz_rem = phdr->p_filesz;
   size_t tot_read = 0;

   *end_vaddr_ref = (uptr)vaddr + (page_count << PAGE_SHIFT);

   ret = vfs_seek(elf_file, (s64)phdr->p_offset, SEEK_SET);

   if (ret != (ssize_t)phdr->p_offset)
      return -ENOEXEC;

   for (u32 j = 0; j < page_count; j++, vaddr += PAGE_SIZE) {

      void *p;

      if (!is_mapped(pdir, vaddr)) {

         p = kzmalloc(PAGE_SIZE);

         if (!p)
            return -ENOMEM;

         rc = map_page(pdir, vaddr, KERNEL_VA_TO_PA(p), true, true);

         if (rc != 0)
            return rc;

      } else {

         uptr pa = get_mapping(pdir, vaddr);
         p = KERNEL_PA_TO_VA(pa);
      }

      if (filesz_rem > 0) {
         size_t off = (va & OFFSET_IN_PAGE_MASK);
         size_t page_rem = PAGE_SIZE - off;
         size_t to_read = MIN(filesz_rem, page_rem);

         ret = vfs_read(elf_file, p + off, to_read);

         if (ret < (ssize_t)to_read)
            return -ENOEXEC;

         tot_read += to_read;
         va += to_read;
         filesz_rem -= to_read;
      }
   }

   ASSERT(tot_read == phdr->p_filesz);
   return 0;
}

static void
phdr_adjust_page_access(pdir_t *pdir, Elf_Phdr *phdr)
{
   char *vaddr = (char *) (phdr->p_vaddr & PAGE_MASK);

   if (phdr->p_memsz == 0)
      return; /* very weird (because the phdr has type LOAD) */

   uptr sz = phdr->p_vaddr + phdr->p_memsz - (uptr)vaddr;
   size_t page_count = (sz + PAGE_SIZE - 1) / PAGE_SIZE;

   /* Make the read-only pages to be read-only */
   for (size_t j = 0; j < page_count; j++, vaddr += PAGE_SIZE)
      if (!(phdr->p_flags & PF_W))
         set_page_rw(pdir, vaddr, false);
}

typedef struct {

   Elf_Ehdr header;
   Elf_Phdr *phdrs;
   size_t total_phdrs_size;

} elf_headers;

static void free_elf_headers(elf_headers *eh)
{
   if (!eh)
      return;

   if (eh->total_phdrs_size)
      kfree2(eh->phdrs, eh->total_phdrs_size);
}

static int load_elf_headers(fs_handle elf_file, elf_headers *eh)
{
   ssize_t rc;
   bzero(eh, sizeof(*eh));

   rc = vfs_seek(elf_file, 0, SEEK_SET);

   if (rc != 0)
      return -EIO;

   rc = vfs_read(elf_file, &eh->header, sizeof(eh->header));

   if (rc < (int)sizeof(eh->header))
      return -ENOEXEC;

   if (strncmp((const char *)eh->header.e_ident, ELFMAG, 4))
      return -ENOEXEC;

   if (eh->header.e_ehsize < sizeof(eh->header))
      return -ENOEXEC;

   eh->total_phdrs_size = eh->header.e_phnum * sizeof(Elf_Phdr);
   eh->phdrs = kmalloc(eh->total_phdrs_size);

   if (!eh->phdrs)
      return -ENOMEM;

   rc = vfs_seek(elf_file, (s64)eh->header.e_phoff, SEEK_SET);

   if (rc != (ssize_t)eh->header.e_phoff) {
      rc = -ENOEXEC;
      goto errend;
   }

   rc = vfs_read(elf_file, eh->phdrs, eh->total_phdrs_size);

   if (rc != (ssize_t)eh->total_phdrs_size) {
      rc = -ENOEXEC;
      goto errend;
   }

   return 0;

errend:
   free_elf_headers(eh);
   return (int) rc;
}

int load_elf_program(const char *filepath,
                     pdir_t **pdir_ref,
                     void **entry,
                     void **stack_addr,
                     void **brk_ref)
{
   fs_handle elf_file = NULL;
   elf_headers eh;
   uptr brk = 0;
   int rc = 0;

   rc = vfs_open(filepath, &elf_file, O_RDONLY, 0);

   if (!rc) {
      rc = load_elf_headers(elf_file, &eh);

      if (!rc) {
         ASSERT(*pdir_ref == NULL);
         *pdir_ref = pdir_clone(get_kernel_pdir());

         if (!*pdir_ref) {
            vfs_close(elf_file);
            rc = -ENOMEM;
         }
      }
   }

   if (rc != 0)
      return rc;

   for (int i = 0; i < eh.header.e_phnum; i++) {

      uptr end_vaddr = 0;
      Elf_Phdr *phdr = eh.phdrs + i;

      if (phdr->p_type != PT_LOAD)
         continue;

      rc = load_phdr(elf_file, *pdir_ref, phdr, &end_vaddr);

      if (rc < 0)
         goto out;

      if (end_vaddr > brk)
         brk = end_vaddr;
   }

   for (int i = 0; i < eh.header.e_phnum; i++) {

      Elf_Phdr *phdr = eh.phdrs + i;

      if (phdr->p_type == PT_LOAD)
         phdr_adjust_page_access(*pdir_ref, phdr);
   }

   // Allocating memory for the user stack.

   const size_t pages_for_stack = USER_STACK_PAGES;
   const uptr stack_top = (USERMODE_VADDR_END - USER_STACK_PAGES * PAGE_SIZE);

#if MMAP_NO_COW

   for (u32 i = 0; i < pages_for_stack; i++) {

      void *p = kzmalloc(PAGE_SIZE);

      if (!p) {
         rc = -ENOMEM;
         goto out;
      }

      rc = map_page(*pdir_ref,
                    (void *)stack_top + (i << PAGE_SHIFT),
                    KERNEL_VA_TO_PA(p),
                    true,
                    true);

      if (rc != 0)
         goto out;
   }

#else

   size_t count = map_zero_pages(*pdir_ref,
                                 (void *)stack_top,
                                 pages_for_stack,
                                 true, true);

   if (count != pages_for_stack) {
      unmap_pages(*pdir_ref, (void *)stack_top, count, true);
      rc = -ENOMEM;
      goto out;
   }

#endif

   // Finally setting the output-params.

   *stack_addr = (void *) USERMODE_STACK_MAX;
   *entry = (void *) eh.header.e_entry;
   *brk_ref = (void *) brk;

out:
   vfs_close(elf_file);
   free_elf_headers(&eh);

   if (LIKELY(!rc)) {

      /* positive case */
      if (LIKELY(get_curr_task() != kernel_process)) {
         task_change_state(get_curr_task(), TASK_STATE_RUNNABLE);
      }

   } else {

      /* error case */
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
   const uptr sym_count = symtab->sh_size / sizeof(Elf_Sym);

   for (uptr i = 0; i < sym_count; i++) {
      Elf_Sym *s = syms + i;

      if (s->st_value <= vaddr && vaddr < s->st_value + s->st_size) {

         if (offset)
            *offset = (ptrdiff_t)(vaddr - s->st_value);

         if (sym_size)
            *sym_size = (u32) s->st_size;

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
   const uptr sym_count = symtab->sh_size / sizeof(Elf_Sym);

   for (uptr i = 0; i < sym_count; i++) {
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
   fault_resumable_call(ALL_FAULTS_MASK, &find_sym_at_addr_no_ret, 4,
                        vaddr, offset, sym_size, &sym_name);

   return sym_name;
}

static Elf_Shdr *kernel_elf_get_section(const char *section_name)
{
   Elf_Ehdr *h = (Elf_Ehdr*)(KERNEL_PA_TO_VA(KERNEL_PADDR));
   Elf_Shdr *sections = (Elf_Shdr *) ((char *)h + h->e_shoff);
   Elf_Shdr *section_header_strtab = sections + h->e_shstrndx;

   for (uint32_t i = 0; i < h->e_shnum; i++) {

      Elf_Shdr *s = sections + i;
      char *name = (char *)h + section_header_strtab->sh_offset + s->sh_name;

      if (!strcmp(name, section_name))
         return s;
   }

   return NULL;
}

void call_kernel_global_ctors(void)
{
   void (*ctor)(void);
   Elf_Shdr *ctors = kernel_elf_get_section(".ctors");

   if (!ctors)
      return;

   void **ctors_begin = (void **)ctors->sh_addr;
   void **ctors_end = (void **)(ctors->sh_addr + ctors->sh_size);

   for (void **p = ctors_begin; p < ctors_end; p++) {

      /*
       * The C99 standard leaves casting from "void *" to a function pointer
       * undefined, that's why we assign write a value at &ctor instead of
       * just casing *p to void (*)(void).
       */
      *(void **)(&ctor) = *p;
      ctor();
   }
}
