
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
    *(.text)
    . = ALIGN(4);
  }

  .rodata :
  {
    *(.rodata)
    . = ALIGN(4096);
  }

  .data :
  {
    *(.data)
    . = ALIGN(4);
  }

  __bss_start = .;
  .bss :
  {
   *(.bss)
   . = ALIGN(4);
  }
   __bss_end = .;
  _end = .;

  /DISCARD/ : { *(.eh_frame*) }
}


