
OUTPUT_FORMAT("binary")

OUTPUT_ARCH(i386)
ENTRY(_start)
SEARCH_DIR("=/tmp/x86-i686--glibc--stable/usr/i686-buildroot-linux-gnu/lib32");
SEARCH_DIR("=/tmp/x86-i686--glibc--stable/usr/i686-buildroot-linux-gnu/lib");

SECTIONS
{
  . = SEGMENT_START("text-segment", 0x0);

  .text :
  {
    *(.text)
    . = @EARLY_BOOT_SZ@;
  }

  /DISCARD/ : { *(*) }
}
