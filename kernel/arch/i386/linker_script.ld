
OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SEARCH_DIR("=/tmp/x86-i686--glibc--stable/usr/i686-buildroot-linux-gnu/lib32");
SEARCH_DIR("=/tmp/x86-i686--glibc--stable/usr/i686-buildroot-linux-gnu/lib");

kernel_base_va      = @CMAKE_KERNEL_BASE_VA@;
kernel_paddr        = @CMAKE_KERNEL_PADDR@;
kernel_text_paddr   = kernel_paddr + 0x1000;

PHDRS
{
   ro_segment PT_LOAD FILEHDR PHDRS;
   rw_segment PT_LOAD;
   special_ro PT_LOAD;
}

SECTIONS
{
   . = SEGMENT_START("text-segment", kernel_base_va + kernel_text_paddr);

   .text : AT(kernel_text_paddr)
   {
      text = .;
      *(.text.unlikely .text.*_unlikely .text.unlikely.*)
      *(.text.exit .text.exit.*)
      *(.text.startup .text.startup.*)
      *(.text.hot .text.hot.*)
      *(.text .stub .text.* .gnu.linkonce.t.*)
   } : ro_segment

   .rodata : AT(kernel_text_paddr + (rodata - text))
   {
      rodata = .;
      *(.rodata .rodata.* .gnu.linkonce.r.*)
   } : ro_segment

   .data ALIGN(4K) : AT(kernel_text_paddr + (data - text))
   {
      data = .;
      *(.data .data.* .gnu.linkonce.d.*)
   } : rw_segment

   .bss : AT(kernel_text_paddr + (bss - text))
   {
      bss = .;
      *(.bss .bss.* .gnu.linkonce.b.*)
   } : rw_segment

   .Symtab ALIGN(4K) : AT(kernel_text_paddr + (Symtab - text))
   {
      Symtab = .;
      *(.Symtab)
   } : special_ro

   .Strtab ALIGN(4): AT(kernel_text_paddr + (Strtab - text))
   {
      Strtab = .;
      *(.Strtab)
   } : special_ro


   _end = .;

   /DISCARD/ : { *(.eh_frame*) }
   /DISCARD/ : { *(.comment) }
}


