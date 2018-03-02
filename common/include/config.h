
#pragma once

/* Utils */

#define KB (1024)
#define MB (1024*1024)

#ifdef DEBUG
#define BUILDTYPE_STR "DEBUG"
#else
#define BUILDTYPE_STR "RELEASE"
#endif

/* ------------------------------- */

/*
 * For the moment, keep those defines always defined.
 * Unless qemu is run with -device isa-debug-exit,iobase=0xf4,iosize=0x04
 * debug_qemu_turn_off_machine() won't turn off the VM.
 */

#define DEBUG_QEMU_EXIT_ON_INIT_EXIT
#define DEBUG_QEMU_EXIT_ON_PANIC


#if !defined(TESTING) && !defined(KERNEL_TEST)

#define KERNEL_BASE_VA             0xC0000000
#define LINEAR_MAPPING_MB          (512)

#else

extern void *kernel_va;
#define KERNEL_BASE_VA             ((uptr)kernel_va)
#define LINEAR_MAPPING_MB          (128)

#endif

#define LINEAR_MAPPING_SIZE        (LINEAR_MAPPING_MB << 20)
#define LINEAR_MAPPING_OVER_END    (KERNEL_BASE_VA + LINEAR_MAPPING_SIZE)

#define KERNEL_PADDR               0x00100000    // +1 MB
#define KERNEL_INITIAL_STACK_ADDR  0xC000FFF0
#define KERNEL_MAX_SIZE            (500 * KB)

#define RAMDISK_PADDR              (2 * MB)
#define RAMDISK_SIZE               (35 * MB)


/* Bootloader specific config */

#define SECTOR_SIZE                512

