
#pragma once


/*
 * For the moment, keep those defines always defined.
 * Unless qemu is run with -device isa-debug-exit,iobase=0xf4,iosize=0x04
 * debug_qemu_turn_off_machine() won't turn off the VM.
 */

#define DEBUG_QEMU_EXIT_ON_INIT_EXIT
#define DEBUG_QEMU_EXIT_ON_PANIC
