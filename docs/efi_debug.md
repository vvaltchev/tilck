How to debug the EFI loader with GDB
-------------------------------------

1. Uncomment the debugging code in efi_main() [for BaseAddr]
2. ./build/run_efi_qemu32
2. gdb
3. target remote :1234
4. add-symbol-file build/boot/efi/ia32/libefi_app_ia32.so <BaseAddr>
5. enjoy debugging

NOTE: `BaseAddr` has a +0x1000 offset from EFI's ImageBase because that's the
vaddr of the first LOAD segment in the binary. For more, run:

- gdb build/boot/efi/ia32/libefi_app_ia32.so
- maintenance info sections

You'll get an output like:

```
(gdb) maintenance info sections
Exec file:
    `./build/boot/efi/ia32/libefi_app_ia32.so', file type elf32-i386.
 [0]     0x0000->0x023c at 0x00001000: .hash ALLOC LOAD READONLY DATA HAS_CONTENTS
 [1]     0x1000->0xa510 at 0x00002000: .text ALLOC LOAD READONLY CODE HAS_CONTENTS
 [2]     0xa510->0xa560 at 0x0000b510: .plt ALLOC LOAD READONLY CODE HAS_CONTENTS
 [3]     0xb000->0xb01c at 0x0000c000: .sdata ALLOC LOAD DATA HAS_CONTENTS
 [4]     0xc000->0x135cc at 0x0000d000: .data ALLOC LOAD DATA HAS_CONTENTS
 [5]     0x14000->0x140a0 at 0x00015000: .dynamic ALLOC LOAD DATA HAS_CONTENTS
 [6]     0x17000->0x174a0 at 0x00018000: .dynsym ALLOC LOAD READONLY DATA HAS_CONTENTS
 [7]     0x15000->0x154c8 at 0x00016000: .rel ALLOC LOAD READONLY DATA HAS_CONTENTS
 [8]     0x16000->0x16020 at 0x00017000: .rel.plt ALLOC LOAD READONLY DATA HAS_CONTENTS
 [9]     0x16020->0x1602a at 0x0001941f: .reloc READONLY HAS_CONTENTS
 [10]     0x18000->0x1841f at 0x00019000: .dynstr ALLOC LOAD READONLY DATA HAS_CONTENTS
 [11]     0x0000->0x0032 at 0x00019429: .comment READONLY HAS_CONTENTS
 [12]     0x0000->0x3626f at 0x0001945b: .debug_info READONLY HAS_CONTENTS
 [13]     0x0000->0x3e2c at 0x0004f6ca: .debug_abbrev READONLY HAS_CONTENTS
 [14]     0x0000->0x0390 at 0x000534f8: .debug_aranges READONLY HAS_CONTENTS
 [15]     0x0000->0x53b9 at 0x00053888: .debug_line READONLY HAS_CONTENTS
 [16]     0x0000->0x4e04 at 0x00058c41: .debug_str READONLY HAS_CONTENTS
 [17]     0x0000->0x02d8 at 0x0005da45: .debug_ranges READONLY HAS_CONTENTS
 [18]     0x0000->0x3eb1 at 0x0005dd1d: .debug_loc READONLY HAS_CONTENTS
```
