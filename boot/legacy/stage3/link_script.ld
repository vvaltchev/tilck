/* SPDX-License-Identifier: BSD-2-Clause */

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")

OUTPUT_ARCH(i386)
ENTRY(_start)
SEARCH_DIR("=/tmp/x86-i686--glibc--stable/usr/i686-buildroot-linux-gnu/lib32");
SEARCH_DIR("=/tmp/x86-i686--glibc--stable/usr/i686-buildroot-linux-gnu/lib");

/*
 * Having the following STARTUP() command un-commented here would be great
 * but the problem with that is that it causes symbol redefinitions because
 * the stage3_entry.S.o would have been added twice to the input object files
 * list (the 2nd time because of STARTUP). Removing stage3_entry.S.o from the
 * input list is tricky because, according to CMake, it is one of the sources of
 * the elf_stage3 target. Making CMake to compile that file AS IF it is a source
 * file, but without being one would require it to be a separate target on
 * which elf_stage3 must depend on. But CMake does not support object files
 * as targets. Therefore it would require a custom target. At the end, the
 * simplest solution to just force stage3_entry.S.o to be the first source file
 * still seems to be least "evil" solution.
 */

/* STARTUP(@STAGE3_STARTUP_OBJ_FILE@) */

SECTIONS
{
   . = SEGMENT_START("text-segment", @BL_BASE_ADDR@);

   .text : AT(text)
   {
      text = .;
      *(.text)
      . = ALIGN(4);
   }

   .rodata : AT(rodata)
   {
      rodata = .;
      *(.rodata*)
      . = ALIGN(4096);
   }

   .data : AT(data)
   {
      data = .;
      *(.data)
      . = ALIGN(32);
   }

   __bss_start = .;
   .bss : AT(__bss_start)
   {
      *(.bss)
      . = ALIGN(4);
   }
   __bss_end = .;

   _end = .;

   /* Completely drop out from the file the following sections */
   /DISCARD/ : { *(.eh_frame*) }
}


