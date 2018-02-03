

/*
OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
*/


OUTPUT_FORMAT("binary")

OUTPUT_ARCH(i386)
ENTRY(_start)
SEARCH_DIR("=/tmp/x86-i686--glibc--stable/usr/i686-buildroot-linux-gnu/lib32");
SEARCH_DIR("=/tmp/x86-i686--glibc--stable/usr/i686-buildroot-linux-gnu/lib");

SECTIONS
{
  . = SEGMENT_START("text-segment", 0x21000);

  .text :
  {
    *(.text.unlikely .text.*_unlikely .text.unlikely.*)
    *(.text.exit .text.exit.*)
    *(.text.startup .text.startup.*)
    *(.text.hot .text.hot.*)
    *(.text .stub .text.* .gnu.linkonce.t.*)
  }

  .rodata :
  {
    *(.rodata .rodata.* .gnu.linkonce.r.*)
  }

  /* Adjust the address for the data segment.  We want to adjust up to
     the same address within the page on the next page up.  */

  . = DATA_SEGMENT_ALIGN (CONSTANT (MAXPAGESIZE), CONSTANT (COMMONPAGESIZE));

  .data :
  {
    *(.data .data.* .gnu.linkonce.d.*)
  }

  __bss_start = .;
  .bss :
  {
   *(.bss .bss.* .gnu.linkonce.b.*)
  }
   __bss_end = .;

  . = ALIGN(32 / 8);
  _end = .;
  . = DATA_SEGMENT_END (.);

  /DISCARD/ : { *(.eh_frame*) }

}


