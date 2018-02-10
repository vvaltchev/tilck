
#pragma once

/* Utils */

#define KB (1024)
#define MB (1024*1024)

/* ------------------------------- */

/*
 * For the moment, keep those defines always defined.
 * Unless qemu is run with -device isa-debug-exit,iobase=0xf4,iosize=0x04
 * debug_qemu_turn_off_machine() won't turn off the VM.
 */

#define DEBUG_QEMU_EXIT_ON_INIT_EXIT
#define DEBUG_QEMU_EXIT_ON_PANIC

#define KERNEL_BASE_VA             0xC0000000

#define RAMDISK_PADDR (128 * MB)
#define RAMDISK_VADDR              0xCA000000
#define RAMDISK_SIZE  (35 * MB)

#define KERNEL_PADDR               0x00100000    // +1 MB
#define KERNEL_INITIAL_STACK_ADDR  0xC000FFF0

#define KERNEL_MAX_SIZE (500 * KB)


/* Bootloader specific config */

#define SECTOR_SIZE               512

