
#include <elf.h>
#include <paging.h>
#include <string_util.h>
#include <arch/generic_x86/utils.h>


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
   printk("Program header entry size: %d\n", h->e_phentsize);
   printk("Program header num entries: %d\n", h->e_phnum);
   printk("Section header entry size: %d\n", h->e_shentsize);
   printk("Section header num entires: %d\n", h->e_shnum);
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

   for (int i = 0; i < h->e_phnum; i++) {
      printk("*** SEGMENT %i ***\n", i);
      dump_elf32_program_segment_header(phdr);
      phdr++;
      printk("\n\n");
   }
}

void load_elf_program(void *elf,
                      page_directory_t *pdir,
                      void **entry,
                      void **stack_addr)
{

   Elf32_Ehdr *header = (Elf32_Ehdr *)elf;
   ASSERT(header->e_ehsize == sizeof(*header));

   dump_elf32_header(header);
   dump_elf32_phdrs(header);

   while(true) halt();
}
