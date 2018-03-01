
OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)
SEARCH_DIR("=/tmp/x86-i686--glibc--stable/usr/i686-buildroot-linux-gnu/lib32");
SEARCH_DIR("=/tmp/x86-i686--glibc--stable/usr/i686-buildroot-linux-gnu/lib");

kernel_base_va = 0xC0000000;

SECTIONS
{
  . = SEGMENT_START("text-segment", kernel_base_va + 0x100000);

  .text :
  {
    *(.text.unlikely .text.*_unlikely .text.unlikely.*)
    *(.text.exit .text.exit.*)
    *(.text.startup .text.startup.*)
    *(.text.hot .text.hot.*)
    *(.text .stub .text.* .gnu.linkonce.t.*)
    *(.rodata .rodata.* .gnu.linkonce.r.*)
    . = ALIGN(4096);
  }

  .data :
  {
    *(.data .data.* .gnu.linkonce.d.*)
    . = ALIGN(4096);
  }

  .bss :
  {
   *(.bss .bss.* .gnu.linkonce.b.*)
  }

  . = ALIGN(4);
  _end = .;

  /DISCARD/ : { *(.eh_frame*) }

}


