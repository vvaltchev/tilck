# TODO: gnu-efi's headers are not symmetric across arches

Status: **open**, not urgent — nothing built today compiles the affected
header. Worth fixing when gnu-efi is next upgraded, or when the arch
builds are made uniform.

## What the patch does

`scripts/patches/target_gnuefi/3.0.17/0001-char16-is-16-bit.diff` makes
two edits, and they are two different things that happen to live in one
file.

### 1. CHAR16 must be 16 bits (ia32, x86_64)

```diff
-typedef wchar_t CHAR16;
+typedef unsigned short CHAR16;
```

UEFI defines CHAR16 as a 16-bit unit; gnu-efi types it as `wchar_t`,
which is 32 bits on our targets. The original bash version stated the
reason and the Ruby port lost it, so it is recorded here:

> force CHAR16 to always be an unsigned short, as the `L"string"`
> literals when `-fshort-wchar` is used. That is necessary because with
> the custom cross musl toolchain built for host=aarch64, wchar_t is
> always defined as "int".

### 2. BOOLEAN/CHAR8 (riscv64 only)

```diff
 #ifndef BOOLEAN
-typedef uint8_t                 BOOLEAN;
+typedef char       CHAR8;
 #endif
```

This one reads like a mistake and is not. `inc/efidef.h` typedefs
BOOLEAN unconditionally:

```c
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
typedef _Bool BOOLEAN;
```

and `inc/riscv64/efibind.h` typedefs it again behind `#ifndef BOOLEAN`
— a guard that **cannot work**, because `#ifndef` tests macros and
BOOLEAN is a typedef. So the guard never fires and the two conflicting
typedefs (`_Bool` and `uint8_t`) collide. That is the "definition
confusion" the commit that introduced it (`d53caaac4`, the riscv64
port) names.

The arches are also asymmetric in the other direction:

```
ia32     CHAR8: yes   BOOLEAN: no
x86_64   CHAR8: yes   BOOLEAN: no
riscv64  CHAR8: no    BOOLEAN: yes (twice, counting efidef.h)
```

So the single substitution removes the duplicate BOOLEAN *and* supplies
the CHAR8 that riscv64 alone is missing. Both halves are deliberate.

## What is actually wrong

**riscv64 never got the CHAR16 fix.** The substitution matched the
literal string `typedef wchar_t CHAR16`, with one space. riscv64 writes
it padded:

```c
typedef wchar_t                 CHAR16;      /* still wchar_t today */
```

so the loop was widened to riscv64 for the BOOLEAN/CHAR8 fix while the
fix it was originally written for silently did not apply there. The
patch converted from that substitution reproduces this faithfully,
because a conversion should not change behaviour.

**Nothing compiles it.** `arch_list: X86_ARCHS.values` — the built
gnuefi package is i386 and x86_64 only, and riscv64 has `efi: nil`.
`kernel/uefi.c` guards its gnu-efi use with

```c
#if !defined(KERNEL_TEST) && (defined(__i386__) || defined(__x86_64__))
```

and the unit tests include headers from `gnuefi_src`, which is
deliberately unpatched. So the riscv64 header is dead in every
configuration we build.

## Options, when the time comes

1. **Drop the riscv64 hunk.** Nothing reads it. Smallest change; loses
   the fix if riscv64 EFI is ever wanted.
2. **Make it correct and complete**: keep BOOLEAN, add CHAR8 as its own
   line, and widen the CHAR16 edit to match riscv64's padding. Then all
   three headers say the same thing and the patch needs no explanation.
3. **Upgrade gnu-efi.** Newer releases may have fixed the `#ifndef
   BOOLEAN` guard and the missing CHAR8 upstream, which would remove
   half the patch. Check before doing (2).

Option 2 is the one that makes the builds symmetric, which is the
property worth having: today the same patch means three different
things in three files, and that is why it took a reading of the
original bash commit to tell a fix from a typo.

## Note

An earlier description of this patch — in the commit message of
`38da1a9c1` and briefly in `toolchain5.md` — called the BOOLEAN hunk a
mistake that "drops BOOLEAN rather than adding CHAR8". That was wrong:
dropping BOOLEAN is the point, because efidef.h already defines it.
This file is the correct account.
