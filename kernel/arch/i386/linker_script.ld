
OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SEARCH_DIR("=/tmp/x86-i686--glibc--stable/usr/i686-buildroot-linux-gnu/lib32");
SEARCH_DIR("=/tmp/x86-i686--glibc--stable/usr/i686-buildroot-linux-gnu/lib");

kernel_base_va      = 0xC0000000;
kernel_paddr        = 0x00100000;
kernel_text_paddr   = kernel_paddr + 0x1000;

PHDRS
{
   ro_segment PT_LOAD FILEHDR PHDRS;
   rw_segment PT_LOAD;
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

   . = ALIGN(4);
   _end = .;

   /DISCARD/ : { *(.eh_frame*) }
   /DISCARD/ : { *(.comment) }
}


