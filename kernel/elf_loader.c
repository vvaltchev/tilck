
#include <elf.h>
#include <paging.h>
#include <process.h>
#include <kmalloc.h>

#include <string_util.h>
#include <fs/exvfs.h>

#ifdef BITS32
#ifdef DEBUG

void dump_elf32_header(Elf32_Ehdr *h)
{
   printk("Magic: ");
   for (int i = 0; i < EI_NIDENT; i++) {
      printk("%x ", h->e_ident[i]);
   }

   printk("\n");
   printk("Type: %p\n", h->e_type);
   printk("Machine: %p\n", h->e_machine);
   printk("Entry point: %p\n", h->e_entry);
   printk("ELF header size: %d\n", h->e_ehsize);
   printk("e_phentsize:  %d\n", h->e_phentsize);
   printk("e_phnum:      %d\n", h->e_phnum);
   printk("e_shentsize:  %d\n", h->e_shentsize);
   printk("e_shnum:      %d\n", h->e_shnum);
   printk("Section header string table index: %d\n\n", h->e_shstrndx);
}

void dump_elf32_program_segment_header(Elf32_Phdr *ph)
{
   printk("Segment type: %d\n", ph->p_type);
   printk("Segment offset in file: %d\n", ph->p_offset);
   printk("Segment vaddr: %p\n", ph->p_vaddr);
   printk("Segment paddr: %p\n", ph->p_paddr);
   printk("Segment size in file: %d\n", ph->p_filesz);
   printk("Segment size in memory: %d\n", ph->p_memsz);
   printk("Segment flags: %u\n", ph->p_flags);
   printk("Segment alignment: %d\n", ph->p_align);
}

void dump_elf32_phdrs(Elf32_Ehdr *h)
{
   Elf32_Phdr *phdr = (Elf32_Phdr *) ((char *)h + sizeof(*h));

   for (int i = 0; i < h->e_phnum; i++, phdr++) {
      printk("*** SEGMENT %i ***\n", i);
      dump_elf32_program_segment_header(phdr);
      printk("\n\n");
   }
}

#endif



void dump_kernel_symtab(void)
{
   fs_handle *elf_file = exvfs_open("/EFI/BOOT/elf_kernel_stripped");
   char *buf = kmalloc(100*1024);
   exvfs_read(elf_file, buf, 100*1024);

   Elf32_Ehdr *h = (Elf32_Ehdr*)buf;
   VERIFY(h->e_shentsize == sizeof(Elf32_Shdr));

   Elf32_Shdr *sections = (Elf32_Shdr *) ((char *)h + h->e_shoff);
   Elf32_Shdr *section_header_strtab = sections + h->e_shstrndx;

   Elf32_Shdr *symtab = NULL;
   Elf32_Shdr *strtab = NULL;

   for (u32 i = 0; i < h->e_shnum; i++) {
      Elf32_Shdr *s = sections + i;
      char *name = (char *)h + section_header_strtab->sh_offset + s->sh_name;
      printk("section: '%s', vaddr: %p, size: %u\n", name, s->sh_addr, s->sh_size);

      if (s->sh_type == SHT_SYMTAB) {
         symtab = s;
      } else if (s->sh_type == SHT_STRTAB && i != h->e_shstrndx) {
         strtab = s;
      }
   }

   printk("Symbols:\n");
   Elf32_Sym *syms = (Elf32_Sym *) ((char *)h + symtab->sh_offset);

   for (u32 i = 0; i < 10; i++) {
      Elf32_Sym *s = syms + i;
      char *name = (char *)h + strtab->sh_offset + s->st_name;
      printk("%p: %s\n", s->st_value, name);
   }

   kfree(buf, 100*1024);
   exvfs_close(elf_file);
}

//////////////////////////////////////////////////////////////////////////////

void load_elf_program(const char *filepath,
                      page_directory_t **pdir_ref,
                      void **entry,
                      void **stack_addr)
{
   ssize_t ret;
   Elf32_Ehdr header;

   fs_handle *elf_file = exvfs_open(filepath);

   if (!elf_file) {
      panic("[kernel] Unable to open '%s'!\n", filepath);
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
   Elf32_Phdr *phdr = kmalloc(total_phdrs_size);
   VERIFY(phdr != NULL);

   ret = exvfs_seek(elf_file, header.e_phoff, SEEK_SET);
   VERIFY(ret == (ssize_t)header.e_phoff);

   ret = exvfs_read(elf_file, phdr, total_phdrs_size);
   ASSERT(ret == total_phdrs_size);

   for (int i = 0; i < header.e_phnum; i++, phdr++) {

      // Ignore non-load segments.
      if (phdr->p_type != PT_LOAD) {
         continue;
      }

      int pages_count =
         ((phdr->p_memsz + PAGE_SIZE) & PAGE_MASK) >> PAGE_SHIFT;

      // printk("[ELF LOADER] Segment %i\n", i);
      // printk("[ELF LOADER] Mem Size: %i\n", phdr->p_memsz);
      // printk("[ELF LOADER] Vaddr: %p\n", phdr->p_vaddr);

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

   kfree(phdr, sizeof(*phdr));
}

#endif // BITS32
