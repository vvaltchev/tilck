
#pragma once

/* Utils */

#define KB (1024)
#define MB (1024*1024)

#define VALUE_1K        0x400
#define VALUE_4K       0x1000
#define VALUE_64K     0x10000

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
#define BASE_LOAD_SEG          0x07C0
#define DEST_DATA_SEGMENT      0x2000
#define TEMP_DATA_SEGMENT      0x1000
#define COMPLETE_FLUSH_ADDR    0x1000
#define COMPLETE_FLUSH_SIZE       128


#define RAMDISK_FIRST_SECTOR 2048

/*
 * TODO: fix the number of sectors, since the fatpart is now bigger!
 * This would require to fix the function lba_to_chs to work with LBA addresses
 * bigger than 65535.
 */

//2048 + 16 MB - 1 (TEMPORARY LIE)
#define RAMDISK_LAST_SECTOR (RAMDISK_FIRST_SECTOR + (32*MB)/SECTOR_SIZE - 1)

//DEBUG VALUE, usable until everything fits in 4 MB
//#define RAMDISK_LAST_SECTOR (RAMDISK_FIRST_SECTOR + (4 * MB)/SECTOR_SIZE - 1)


/*
 * We're OK with just 32 KB.
 * That's because now the initial sectors contain just the 3rd stage of the
 * bootloader. The actual kernel file is loaded by the bootloader from the
 * FAT32 ramdisk.
 */

#define INITIAL_SECTORS_TO_READ (32*KB/SECTOR_SIZE)
