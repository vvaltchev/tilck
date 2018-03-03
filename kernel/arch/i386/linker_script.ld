
OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)
SEARCH_DIR("=/tmp/x86-i686--glibc--stable/usr/i686-buildroot-linux-gnu/lib32");
SEARCH_DIR("=/tmp/x86-i686--glibc--stable/usr/i686-buildroot-linux-gnu/lib");

kernel_base_va = 0xC0000000;
kernel_paddr   = 0x00100000;

SECTIONS
{
  . = SEGMENT_START("text-segment", kernel_base_va + kernel_paddr);

  .text : AT(kernel_paddr)
  {
    text = .;
    *(.text.unlikely .text.*_unlikely .text.unlikely.*)
    *(.text.exit .text.exit.*)
    *(.text.startup .text.startup.*)
    *(.text.hot .text.hot.*)
    *(.text .stub .text.* .gnu.linkonce.t.*)
    . = ALIGN(4);
  }

   .rodata : AT(kernel_paddr + (rodata - text))
   {
      rodata = .;
      *(.rodata .rodata.* .gnu.linkonce.r.*)
      . = ALIGN(4096);
   }

  .data : AT(kernel_paddr + (data - text))
  {
    data = .;
    *(.data .data.* .gnu.linkonce.d.*)
    . = ALIGN(4096);
  }

  .bss : AT(kernel_paddr + (bss - text))
  {
    bss = .;
    *(.bss .bss.* .gnu.linkonce.b.*)
  }

  . = ALIGN(4);
  _end = .;

  /DISCARD/ : { *(.eh_frame*) }

}


