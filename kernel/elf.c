/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/elf_loader.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/fault_resumable.h>


#if defined(__x86_64__)
   #define ELF_CURR_ARCH   EM_X86_64
   #define ELF_CURR_CLASS  ELFCLASS64
#elif defined(__i386__)
   #define ELF_CURR_ARCH   EM_386
   #define ELF_CURR_CLASS  ELFCLASS32
#else
   #error Architecture not supported.
#endif


static ssize_t
load_phdr(fs_handle *elf_file,
          pdir_t *pdir,
          Elf_Phdr *phdr,
          uptr *end_vaddr_ref)
{
   ssize_t rc, ret;
   uptr va = phdr->p_vaddr;
   size_t filesz_rem = phdr->p_filesz;
   char *vaddr = (char *) (phdr->p_vaddr & PAGE_MASK);
   const size_t memsz = phdr->p_vaddr + phdr->p_memsz - (uptr)vaddr;
   const size_t page_count = (memsz + PAGE_SIZE - 1) / PAGE_SIZE;
   DEBUG_ONLY(size_t tot_read = 0);

   if (UNLIKELY(phdr->p_memsz == 0))
      return 0; /* very weird (because the phdr has type LOAD) */

   *end_vaddr_ref = (uptr)vaddr + (page_count << PAGE_SHIFT);

   ret = vfs_seek(elf_file, (s64)phdr->p_offset, SEEK_SET);

   if (ret != (ssize_t)phdr->p_offset)
      return -ENOEXEC;

   for (u32 j = 0; j < page_count; j++, vaddr += PAGE_SIZE) {

      void *p;

      if (!is_mapped(pdir, vaddr)) {

         if (!(p = kzmalloc(PAGE_SIZE)))
            return -ENOMEM;

         if ((rc = map_page(pdir, vaddr, KERNEL_VA_TO_PA(p), true, true))) {
            kfree2(p, PAGE_SIZE);
            return rc;
         }

      } else {

         /* Get user's vaddr as a kernel vaddr */
         p = KERNEL_PA_TO_VA(get_mapping(pdir, vaddr));
      }

      if (filesz_rem) {

         const size_t off = (va & OFFSET_IN_PAGE_MASK);
         const size_t to_read = MIN(filesz_rem, (PAGE_SIZE - off));

         ret = vfs_read(elf_file, p + off, to_read);

         if (ret < (ssize_t)to_read)
            return -ENOEXEC;

         va += to_read;
         filesz_rem -= to_read;
         DEBUG_ONLY(tot_read += to_read);
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

struct elf_headers {

   char *header_buf;
   Elf_Ehdr *header;
   Elf_Phdr *phdrs;
   size_t total_phdrs_size;
};

static void free_elf_headers(struct elf_headers *eh)
{
   ASSERT(eh != NULL);

   if (eh->total_phdrs_size)
      kfree2(eh->phdrs, eh->total_phdrs_size);
}

static int
load_elf_headers(fs_handle elf_file, char *hdr_buf, struct elf_headers *eh)
{
   ssize_t rc;
   bzero(eh, sizeof(*eh));

   if ((rc = vfs_seek(elf_file, 0, SEEK_SET)))
      return -EIO;

   rc = vfs_read(elf_file, hdr_buf, ELF_RAW_HEADER_SIZE);

   if (rc < (int)sizeof(*eh->header))
      return -ENOEXEC;

   eh->header = (void *)hdr_buf;

   if (strncmp((const char *)eh->header->e_ident, ELFMAG, 4))
      return -ENOEXEC;

   if (eh->header->e_ident[EI_CLASS] != ELF_CURR_CLASS)
      return -ENOEXEC;

   if (eh->header->e_type != ET_EXEC)
      return -ENOEXEC;

   if (eh->header->e_machine != ELF_CURR_ARCH)
      return -ENOEXEC;

   if (eh->header->e_ehsize < sizeof(eh->header))
      return -ENOEXEC;

   eh->total_phdrs_size = eh->header->e_phnum * sizeof(Elf_Phdr);
   eh->phdrs = kmalloc(eh->total_phdrs_size);

   if (!eh->phdrs)
      return -ENOMEM;

   rc = vfs_seek(elf_file, (s64)eh->header->e_phoff, SEEK_SET);

   if (rc != (ssize_t)eh->header->e_phoff) {
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

static int
alloc_and_map_stack_page(pdir_t *pdir, void *stack_top, u32 i)
{
   int rc;
   void *p = kzmalloc(PAGE_SIZE);

   if (!p)
      return -ENOMEM;

   rc = map_page(pdir,
                 (void *)stack_top + (i << PAGE_SHIFT),
                 KERNEL_VA_TO_PA(p),
                 true,
                 true);

   return rc;
}

int load_elf_program(const char *filepath,
                     char *header_buf,
                     pdir_t **pdir_ref,
                     void **entry,
                     void **stack_addr,
                     void **brk_ref)
{
   fs_handle elf_file = NULL;
   struct stat64 statbuf;
   struct elf_headers eh;
   uptr brk = 0;
   size_t count;
   int rc;

   if ((rc = vfs_open(filepath, &elf_file, O_RDONLY, 0)))
      return rc;

   if ((rc = vfs_fstat64(elf_file, &statbuf))) {
      vfs_close(elf_file);
      return rc;
   }

   if (!(statbuf.st_mode & S_IXUSR)) {
      vfs_close(elf_file);
      return -EACCES;
   }

   if ((rc = load_elf_headers(elf_file, header_buf, &eh))) {
      vfs_close(elf_file);
      return rc;
   }

   ASSERT(*pdir_ref == NULL);

   if (!(*pdir_ref = pdir_clone(get_kernel_pdir()))) {
      rc = -ENOMEM;
      goto out;
   }

   for (int i = 0; i < eh.header->e_phnum; i++) {

      uptr end_vaddr = 0;
      Elf_Phdr *phdr = eh.phdrs + i;

      if (phdr->p_type != PT_LOAD)
         continue;

      rc = (int) load_phdr(elf_file, *pdir_ref, phdr, &end_vaddr);

      if (rc < 0)
         goto out;

      if (end_vaddr > brk)
         brk = end_vaddr;
   }

   for (int i = 0; i < eh.header->e_phnum; i++) {

      Elf_Phdr *phdr = eh.phdrs + i;

      if (phdr->p_type == PT_LOAD)
         phdr_adjust_page_access(*pdir_ref, phdr);
   }

   /*
    * Mapping the user stack.
    *
    * In the "NOCOW" case, all the pages are pre-allocated.
    * In the default case instead, most of the pages are zero-mapped and so,
    * therefore, allocated on-demand. But, `USER_ARGS_PAGE_COUNT` pages are
    * pre-allocated anyway.
    */

   const size_t pre_allocated_pages =
      MMAP_NO_COW ? USER_STACK_PAGES : USER_ARGS_PAGE_COUNT;

   const size_t zero_mapped_pages = USER_STACK_PAGES - pre_allocated_pages;
   const uptr stack_top = (USERMODE_VADDR_END - USER_STACK_PAGES * PAGE_SIZE);

   count = map_zero_pages(*pdir_ref,
                          (void *)stack_top,
                          zero_mapped_pages,
                          true, true);

   if (count != zero_mapped_pages) {
      unmap_pages(*pdir_ref, (void *)stack_top, count, true);
      rc = -ENOMEM;
      goto out;
   }

   for (u32 i = zero_mapped_pages; i < USER_STACK_PAGES; i++) {
      if ((rc = alloc_and_map_stack_page(*pdir_ref, (void *)stack_top, i)))
         goto out;
   }


   // Finally setting the output-params.

   *stack_addr = (void *) USERMODE_STACK_MAX;
   *entry = (void *) eh.header->e_entry;
   *brk_ref = (void *) brk;

out:
   vfs_close(elf_file);
   free_elf_headers(&eh);

   if (UNLIKELY(rc != 0)) {
      if (*pdir_ref) {
         pdir_destroy(*pdir_ref);
         *pdir_ref = NULL;
      }
   }

   return rc;
}

void get_symtab_and_strtab(Elf_Shdr **symtab, Elf_Shdr **strtab)
{
   Elf_Ehdr *h = (Elf_Ehdr*)(KERNEL_PA_TO_VA(KERNEL_PADDR));
   *symtab = NULL;
   *strtab = NULL;

   if (!KERNEL_SYMBOLS)
      return;

   VERIFY(h->e_shentsize == sizeof(Elf_Shdr));
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

   if (!KERNEL_SYMBOLS)
      return NULL;

   get_symtab_and_strtab(&symtab, &strtab);

   Elf_Sym *syms = (Elf_Sym *) symtab->sh_addr;
   const uptr sym_count = symtab->sh_size / sizeof(Elf_Sym);

   for (uptr i = 0; i < sym_count; i++) {
      Elf_Sym *s = syms + i;

      if (IN_RANGE(vaddr, s->st_value, s->st_value + s->st_size)) {

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

   if (!KERNEL_SYMBOLS)
      return 0;

   get_symtab_and_strtab(&symtab, &strtab);

   Elf_Sym *syms = (Elf_Sym *) symtab->sh_addr;
   const uptr sym_count = symtab->sh_size / sizeof(Elf_Sym);

   for (uptr i = 0; i < sym_count; i++) {
      if (!strcmp((char *)strtab->sh_addr + syms[i].st_name, searched_sym))
         return syms[i].st_value;
   }

   return 0;
}

int foreach_symbol(int (*cb)(struct elf_symbol_info *, void *), void *arg)
{
   Elf_Shdr *symtab;
   Elf_Shdr *strtab;
   int ret = 0;

   if (!KERNEL_SYMBOLS)
      return 0;

   get_symtab_and_strtab(&symtab, &strtab);

   Elf_Sym *syms = (Elf_Sym *) symtab->sh_addr;
   const uptr sym_count = symtab->sh_size / sizeof(Elf_Sym);

   for (uptr i = 0; i < sym_count; i++) {

      Elf_Sym *s = syms + i;

      struct elf_symbol_info info = {
         .vaddr = TO_PTR(s->st_value),
         .size = (u32) s->st_size,
         .name = (char *)strtab->sh_addr + syms[i].st_name,
      };

      if ((ret = cb(&info, arg)))
         break;
   }

   return ret;
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

   if (!ctors) {

      ctors = kernel_elf_get_section(".init_array");

      if (!ctors)
         return;
   }

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
