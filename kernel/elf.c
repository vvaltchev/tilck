/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_mm.h>
#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/elf_loader.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/fault_resumable.h>
#include <tilck/kernel/fs/flock.h>

#include <sys/mman.h>      // system header

#if defined(__x86_64__)
   #define ELF_CURR_ARCH   EM_X86_64
   #define ELF_CURR_CLASS  ELFCLASS64
#elif defined(__i386__)
   #define ELF_CURR_ARCH   EM_386
   #define ELF_CURR_CLASS  ELFCLASS32
#elif defined(__aarch64__)
   #define ELF_CURR_ARCH   EM_AARCH64
   #define ELF_CURR_CLASS  ELFCLASS64
#else
   #error Architecture not supported.
#endif

typedef int (*load_segment_func)(fs_handle *, pdir_t *, Elf_Phdr *, ulong *);

static int
load_segment_by_copy(fs_handle *elf_h,
                     pdir_t *pdir,
                     Elf_Phdr *phdr,
                     ulong *end_vaddr_ref)
{
   offt rc;
   ulong va = phdr->p_vaddr;
   size_t filesz_rem = phdr->p_filesz;
   char *vaddr = (char *) (phdr->p_vaddr & PAGE_MASK);
   const size_t memsz = phdr->p_vaddr + phdr->p_memsz - (ulong)vaddr;
   const size_t page_count = (memsz + PAGE_SIZE - 1) / PAGE_SIZE;
   DEBUG_ONLY(size_t tot_read = 0);

   if (UNLIKELY(phdr->p_memsz == 0))
      return 0; /* very weird (because the phdr has type LOAD) */

   *end_vaddr_ref = (ulong)vaddr + (page_count << PAGE_SHIFT);

   rc = vfs_seek(elf_h, (offt)phdr->p_offset, SEEK_SET);

   if (rc < 0)
      return (int)rc; /* I/O error during seek */

   if (rc != (ssize_t)phdr->p_offset)
      return -ENOEXEC;

   for (u32 j = 0; j < page_count; j++, vaddr += PAGE_SIZE) {

      void *p;

      if (!is_mapped(pdir, vaddr)) {

         if (!(p = kzmalloc(PAGE_SIZE)))
            return -ENOMEM;

         if ((rc = map_page(pdir, vaddr, LIN_VA_TO_PA(p), PAGING_FL_RWUS))) {
            kfree2(p, PAGE_SIZE);
            return (int)rc;
         }

      } else {

         /* Get user's vaddr as a kernel vaddr */
         p = PA_TO_LIN_VA(get_mapping(pdir, vaddr));
      }

      if (filesz_rem) {

         const size_t off = (va & OFFSET_IN_PAGE_MASK);
         const size_t to_read = MIN(filesz_rem, (PAGE_SIZE - off));

         rc = vfs_read(elf_h, p + off, to_read);

         if (rc < 0)
            return (int)rc;           /* I/O error during read */

         if (rc < (ssize_t)to_read)
            return -ENOEXEC;      /* The ELF file is corrupted */

         va += to_read;
         filesz_rem -= to_read;
         DEBUG_ONLY(tot_read += to_read);
      }
   }

   ASSERT(tot_read == phdr->p_filesz);

   if (!(phdr->p_flags & PF_W)) {

      /* Make the read-only pages to be read-only */
      vaddr = (char *) (phdr->p_vaddr & PAGE_MASK);

      for (size_t j = 0; j < page_count; j++, vaddr += PAGE_SIZE)
         set_page_rw(pdir, vaddr, false);
   }

   return 0;
}

static inline int check_segment_alignment(Elf_Phdr *phdr)
{
   const ulong file_page_off = phdr->p_offset & OFFSET_IN_PAGE_MASK;
   const ulong mem_page_off = phdr->p_vaddr & OFFSET_IN_PAGE_MASK;

   if (phdr->p_align != PAGE_SIZE) {

      /*
       * We simply cannot possibly support such a scenario. It would imply
       * having cases like:
       *
       *    file_offset   = 0x0000a001
       *    memory_offset = 0x0800b002
       *
       * While we can only map 4k pages, like mapping the whole 4k chunk of
       * the ELF file at 0x0000a000 to the virtual memory address 0x800b000.
       * If the two offsets are not equal, the whole mapping in the memory would
       * have to shift by a given amount in the range [1, 4095] and that could
       * possibily happen only after copying the whole file in memory.
       *
       * NOTE: while it might seem that p_align == PAGE_SIZE means that both
       * file_page_off and mem_page_off must always be zero, that's NOT correct.
       * In reality, it's typical for them to have values != 0 as long as their
       * value is the same, like 0x123, that's absolutely fine. Just, during
       * the mapping, that offset will be simply ignored.
       *
       * See the case below.
       */

      return -ENOEXEC;
   }

   if (file_page_off != mem_page_off) {

      /*
       * The ELF header states that this segment is aligned at PAGE_SIZE but
       * the in-page offset of the file position and the one of the vaddr
       * differ: therefore, the header is lying. The segment is NOT aligned at
       * PAGE_SIZE. We cannot possibly accept that.
       */
      return -ENOEXEC;
   }

   return 0;
}

static int
load_segment_by_mmap(fs_handle *elf_h,
                     pdir_t *pdir,
                     Elf_Phdr *phdr,
                     ulong *end_vaddr_ref)
{
   if (phdr->p_flags & PF_W)
      return load_segment_by_copy(elf_h, pdir, phdr, end_vaddr_ref);

   if (UNLIKELY(phdr->p_memsz == 0))
      return 0; /* very weird (because the phdr has type LOAD) */

   /*
    * Logic behind the calculation of `um.len`.
    *
    * First of all, phdr->p_memsz is NOT page aligned; it could have any value.
    * Because we have to map a number of pages, not bytes, the least we can do
    * is to round-up the value of `p_memsz` at PAGE_SIZE. That would do a great
    * job for many cases like:
    *
    *    vaddr:  0x08001000
    *    length: 800 bytes -> 4096 bytes (1 page)
    *       => range [0x08001000, 0x08002000)
    *
    * BUT, because `p_vaddr` is NOT required to be divisible by PAGE_SIZE, see
    * check_segment_alignment(), we will end with a wrong range in some cases.
    * For example:
    *
    *    vaddr: 0x08001c00       (in-page offset: 0xc00 = 3072)
    *    length: 2048 bytes -> 4096 bytes (1 page)
    *         => range [0x08001000, 0x08002000)     <---- WRONG!!
    *
    * The correct way of calculating `length` requires to consider the in-page
    * offset of `vaddr` as part of it. In other words:
    *
    *    0x08001c00 (vaddr) - 0x08001000 (vaddr & PAGE_MASK) + 2048 (p_memsz) =
    *    0xc00 + 2048 = 5120 -> 8192 (2 pages)
    *       => range [0x08001000, 0x08003000)      <---- CORRECT!!
    */

   struct user_mapping um = {0};
   um.pi = NULL;
   um.h = elf_h;
   um.off = phdr->p_offset & PAGE_MASK;
   um.vaddr = phdr->p_vaddr & PAGE_MASK;
   um.len = round_up_at(phdr->p_vaddr + phdr->p_memsz - um.vaddr, PAGE_SIZE);
   um.prot = PROT_READ;

   *end_vaddr_ref = um.vaddr + um.len;
   return vfs_mmap(&um, pdir, VFS_MM_DONT_REGISTER);
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
load_elf_headers(fs_handle elf_h,
                 char *hdr_buf,
                 struct elf_headers *eh,
                 bool *wrong_arch)
{
   offt rc;
   bzero(eh, sizeof(*eh));

   if ((rc = vfs_seek(elf_h, 0, SEEK_SET)))
      return -EIO;

   rc = vfs_read(elf_h, hdr_buf, ELF_RAW_HEADER_SIZE);

   if (rc < (int)sizeof(*eh->header))
      return -ENOEXEC;

   eh->header = (void *)hdr_buf;

   if (strncmp((const char *)eh->header->e_ident, ELFMAG, 4))
      return -ENOEXEC;

   if (eh->header->e_type != ET_EXEC)
      return -ENOEXEC;

   if (eh->header->e_ident[EI_CLASS] != ELF_CURR_CLASS ||
       eh->header->e_machine != ELF_CURR_ARCH)
   {
      *wrong_arch = true;
      return -ENOEXEC;
   }

   if (eh->header->e_ehsize < sizeof(eh->header))
      return -ENOEXEC;

   eh->total_phdrs_size = eh->header->e_phnum * sizeof(Elf_Phdr);
   eh->phdrs = kmalloc(eh->total_phdrs_size);

   if (!eh->phdrs)
      return -ENOMEM;

   rc = vfs_seek(elf_h, (offt)eh->header->e_phoff, SEEK_SET);

   if (rc != (ssize_t)eh->header->e_phoff) {
      rc = -ENOEXEC;
      goto errend;
   }

   rc = vfs_read(elf_h, eh->phdrs, eh->total_phdrs_size);

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
                 LIN_VA_TO_PA(p),
                 PAGING_FL_RW | PAGING_FL_US);

   return rc;
}

static int
open_elf_file(const char *filepath, fs_handle *elf_file_ref)
{
   struct k_stat64 statbuf;
   fs_handle h;
   int rc;

   if ((rc = vfs_open(filepath, &h, O_RDONLY, 0)))
      return rc;           /* The file does not exist (typical case) */

   if ((rc = vfs_fstat64(h, &statbuf))) {
      vfs_close(h);
      return rc;           /* Cannot stat() the file */
   }

   if ((statbuf.st_mode & S_IFREG) != S_IFREG) {

      vfs_close(h);

      if ((statbuf.st_mode & S_IFDIR) == S_IFDIR)
         return -EISDIR;   /* Cannot execute a directory! */

      return -EACCES;      /* Not a regular file */
   }

   if ((statbuf.st_mode & S_IXUSR) != S_IXUSR) {
      vfs_close(h);
      return -EACCES;      /* Doesn't have exec permission */
   }

   *elf_file_ref = h;
   return 0;
}

static bool
is_dyn_exec(struct elf_headers *eh)
{
   Elf_Ehdr *hdr = eh->header;

   for (int i = 0; i < hdr->e_phnum; i++) {

      Elf_Phdr *phdr = eh->phdrs + i;

      if (phdr->p_type == PT_INTERP)
         return true;
   }

   return false;
}

int
load_elf_program(const char *filepath,
                 char *header_buf,
                 struct elf_program_info *pinfo)
{
   load_segment_func load_seg = NULL;
   fs_handle elf_h = NULL;
   struct elf_headers eh;
   ulong brk = 0;
   size_t count;
   int rc;

   pinfo->wrong_arch = false;
   pinfo->dyn_exec = false;

   if ((rc = open_elf_file(filepath, &elf_h)))
      return rc;

   if ((rc = acquire_subsys_flock_h(elf_h, SUBSYS_PROCMGNT, &pinfo->lf))) {
      vfs_close(elf_h);
      return rc == -EBADF ? -ENOEXEC : rc;
   }

   if ((rc = load_elf_headers(elf_h, header_buf, &eh, &pinfo->wrong_arch))) {
      vfs_close(elf_h);
      return rc;
   }

   if (is_dyn_exec(&eh)) {
      pinfo->dyn_exec = true;
      rc = -ENOEXEC;
      goto out;
   }

   load_seg = is_mmap_supported(elf_h)
      ? &load_segment_by_mmap
      : &load_segment_by_copy;

   ASSERT(pinfo->pdir == NULL);

   if (!(pinfo->pdir = pdir_clone(get_kernel_pdir()))) {
      rc = -ENOMEM;
      goto out;
   }

   for (int i = 0; i < eh.header->e_phnum; i++) {

      ulong end_vaddr = 0;
      Elf_Phdr *phdr = eh.phdrs + i;

      if (phdr->p_type != PT_LOAD)
         continue;

      rc = check_segment_alignment(phdr);

      if (rc < 0)
         goto out;

      rc = load_seg(elf_h, pinfo->pdir, phdr, &end_vaddr);

      if (rc < 0)
         goto out;

      if (end_vaddr > brk)
         brk = end_vaddr;
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
   const ulong stack_top = (USERMODE_VADDR_END - USER_STACK_PAGES * PAGE_SIZE);

   count = map_zero_pages(pinfo->pdir,
                          (void *)stack_top,
                          zero_mapped_pages,
                          PAGING_FL_US | PAGING_FL_RW);

   if (count != zero_mapped_pages) {
      unmap_pages(pinfo->pdir, (void *)stack_top, count, true);
      rc = -ENOMEM;
      goto out;
   }

   for (u32 i = zero_mapped_pages; i < USER_STACK_PAGES; i++) {
      if ((rc = alloc_and_map_stack_page(pinfo->pdir, (void *)stack_top, i)))
         goto out;
   }


   // Finally setting the output-params.

   pinfo->stack = (void *) USERMODE_STACK_MAX;
   pinfo->entry = (void *) eh.header->e_entry;
   pinfo->brk = (void *) brk;

out:
   vfs_close(elf_h);
   free_elf_headers(&eh);

   if (UNLIKELY(rc != 0)) {

      if (pinfo->pdir) {
         pdir_destroy(pinfo->pdir);
         pinfo->pdir = NULL;
      }

      if (pinfo->lf)
         release_subsys_flock(pinfo->lf);
   }

   return rc;
}

void get_symtab_and_strtab(Elf_Shdr **symtab, Elf_Shdr **strtab)
{
   Elf_Ehdr *h = (Elf_Ehdr*)KERNEL_VADDR;
   *symtab = NULL;
   *strtab = NULL;

   if (!KERNEL_SYMBOLS)
      return;

   VERIFY(h->e_shentsize == sizeof(Elf_Shdr));
   Elf_Shdr *sections = (Elf_Shdr *)((void *)h + h->e_shoff);

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

const char *find_sym_at_addr(ulong vaddr, long *offset, u32 *sym_size)
{
   Elf_Shdr *symtab;
   Elf_Shdr *strtab;

   if (!KERNEL_SYMBOLS)
      return NULL;

   get_symtab_and_strtab(&symtab, &strtab);

   Elf_Sym *syms = (Elf_Sym *) symtab->sh_addr;
   const ulong sym_count = symtab->sh_size / sizeof(Elf_Sym);

   for (ulong i = 0; i < sym_count; i++) {
      Elf_Sym *s = syms + i;

      if (IN_RANGE(vaddr, s->st_value, s->st_value + s->st_size)) {

         if (offset)
            *offset = (long)(vaddr - s->st_value);

         if (sym_size)
            *sym_size = (u32) s->st_size;

         return (char *)strtab->sh_addr + s->st_name;
      }
   }

   return NULL;
}

ulong find_addr_of_symbol(const char *searched_sym)
{
   Elf_Shdr *symtab;
   Elf_Shdr *strtab;

   if (!KERNEL_SYMBOLS)
      return 0;

   get_symtab_and_strtab(&symtab, &strtab);

   Elf_Sym *syms = (Elf_Sym *) symtab->sh_addr;
   const ulong sym_count = symtab->sh_size / sizeof(Elf_Sym);

   for (ulong i = 0; i < sym_count; i++) {
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
   const ulong sym_count = symtab->sh_size / sizeof(Elf_Sym);

   for (ulong i = 0; i < sym_count; i++) {

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
find_sym_at_addr_no_ret(ulong vaddr,
                        long *offset,
                        u32 *sym_size,
                        const char **sym_name_ref)
{
  *sym_name_ref = find_sym_at_addr(vaddr, offset, sym_size);
}

const char *
find_sym_at_addr_safe(ulong vaddr, long *offset, u32 *sym_size)
{
   const char *sym_name = NULL;
   fault_resumable_call(ALL_FAULTS_MASK, &find_sym_at_addr_no_ret, 4,
                        vaddr, offset, sym_size, &sym_name);

   return sym_name;
}

static Elf_Shdr *kernel_elf_get_section(const char *section_name)
{
   Elf_Ehdr *h = (Elf_Ehdr*)KERNEL_VADDR;
   Elf_Shdr *sections = (Elf_Shdr *)((void *)h + h->e_shoff);
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
