# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository.

## Project Overview

Tilck is an educational monolithic kernel designed to be Linux-compatible at
binary level. It runs on i386 (primary), riscv64, and x86_64. It implements
~100 Linux syscalls and runs mainstream Linux programs (BusyBox, Vim, TinyCC,
Micropython, Lua) without custom rewrites. ~13,300 lines of kernel code.
Licensed BSD 2-Clause.

## Originality and licensing (BSD-2-Clause)

Tilck must remain 100% original work. **Never copy implementation code,
identifiers, struct layouts, or macro idioms from Linux, glibc, or any
GPL/copyleft/incompatibly-licensed source.** High-level *ideas* and
well-known techniques (exception tables, scheduler designs, etc.) are fine to
learn from and reimplement — the concrete code must be written from scratch in
Tilck's style. When borrowing a technique from Linux/FreeBSD, give it a
Tilck-native name and a from-scratch implementation; don't mirror their
identifiers (`__ex_table`, `fixup_exception`, `pcb_onfault`, ...). If unsure
whether something crosses from idea into copying, stop and ask.

## Boot time and runtime latency are non-negotiable

Target: embedded systems with hard-realtime ambitions. Evaluate
proposals against that future, not "it's educational today".

**Boot time is sacred.** Anchor numbers under pure QEMU emulation
(TCG, no KVM): full boot through the custom bootloader **under 100
ms**; loaded with `-kernel` **under 50 ms**. Order-of-magnitude
budget:

- **100 ns to ~1 us**: free. Initialize a kcond, install an IRQ
  handler, zero a struct at boot even if the resource isn't used
  yet. Don't contort code with lazy-init / one-time CAS /
  constructor-on-first-use to save a `kcond_init`.
- **~10 us to ~1 ms**: prefer to defer, not a hard rule. If the
  work is needed during boot anyway, just do it.
- **>1 ms, especially seconds**: hard rejection. Do the work async
  (kthread, worker job, lazy on first real use) or drop the
  feature.

Mistake-to-not-repeat: proposing a synchronous busy-wait for the
RTC second edge in `init_system_time()` to get a precise
`boot_timestamp` — would cost up to 1s of boot on the critical
path, for a feature an async kthread already covers. Right shape:
make the kthread simpler/faster (RTC UIE instead of polling), don't
drag work into boot.

Same principle for runtime hot paths (timer IRQ, scheduler tick,
syscall entry/exit, context switch, IRQ handlers). Don't add work
to a hot path to make a cold path cleaner.

## Working with Git History

**Check `docs/annotated-commit-history.txt` before `git log`/`show`/`blame`.**
Plain-text, 80-col walk of every first-parent commit on master
(~4,900 commits, March 2016+); each entry has the verbatim log
header plus an "AI notes:" summary of the diff, large refactors
grouped into arcs. Read via `less`, `/`-search for subsystem or
hash. Falls through to raw git only when annotation is too shallow.

`scripts/dev/claude/annotate_commits` builds and extends it
(sub-commands: regen-list / meta-range / merges / merge-log /
append / show-head / sha-meta).

## Your Dedicated Tools Directory: `scripts/dev/claude/`

Dedicated home for helper scripts I write for my own work. When a
task needs many related shell ops (git plumbing, file scans, repeated
pipelines), write a sub-command dispatcher here instead of one-shot
commands — the user grants permission once for
`Bash(scripts/dev/claude/<tool>:*)` and the workflow stays auditable.
Canonical pattern: `scripts/dev/claude/annotate_commits`
(argument-parsed sub-commands, single permission pattern, dev-only).

## AI-generated plans and specs: `docs/plans/`

Plans, designs, and feature specs I generate go in `docs/plans/` (kept for
history). The root-level `docs/*.md` (contributing, building, scheduler, etc.)
are the human-facing docs — keep them very readable and uncluttered; never drop
an AI working document at `docs/` root.

## Build Commands

```bash
# First-time per arch (i386 | riscv64 | x86_64)
export ARCH=i386
./scripts/build_toolchain                # cross-compiler toolchain
./scripts/build_toolchain -s host_gtest  # unit test deps
./scripts/build_toolchain {-l,-h}        # list / help

# Configure (cmake_run forwards to cmake; runs from root or out-of-tree)
./scripts/cmake_run                      # default debug, ARCH=i386
./scripts/cmake_run -DRELEASE=1          # -O3
./scripts/cmake_run -DDEBUG_CHECKS=0
./scripts/cmake_run -DARCH={riscv64,x86_64}
./scripts/cmake_run --contrib            # clang + -Wconversion + stress opts

# Build (cmake_run auto-runs if needed)
make             # one file at a time, good for debugging
make -j          # parallel (don't use -j$(nproc))
make gtests      # unit tests (needs gtest/gmock)
make rem         # rebuild image only (drops fatpart + tilck.img)

# Out-of-tree
mkdir build-dir && cd build-dir && /abs/path/scripts/cmake_run && make -j

# All configs (slow, useful before push)
./scripts/adv/gen_other_builds  # builds every scripts/build_generators/* config
```

**Build directories are disposable.** `build/`, any out-of-tree `build-*`,
and per-arch build trees are regenerable artifacts — delete, recreate, or
reconfigure them freely (e.g. for a throwaway cross-config build). Nothing
of value is lost: only build output that `cmake_run` + `make` reproduce. No
need to ask before clobbering or removing one.

## Testing

Four test types: unit (gtest, host), selftest (in-kernel), shellcmd
(syscall-based), interactive (`--intr` build).

```bash
# Unit tests (host, needs gtest/gmock) -- ~2s, 10s timeout is plenty
./build/gtests
./build/gtests --gtest_filter=kmalloc_test.*
./build/gtests --gtest_list_tests

# Test runner (boots QEMU for VM types; needs KVM)
./build/st/run_all_tests             # one VM per test
./build/st/run_all_tests -c          # single VM -- ~46s, 60s timeout

# By type (-T prefix-matched: 'u'=unit, 'se'=selftest, 'sh'=shellcmd)
./build/st/run_all_tests -T unit                # host, no VM/KVM -- ~24s
./build/st/run_all_tests -T unit -c             # all in one gtests run
./build/st/run_all_tests -T selftest
./build/st/run_all_tests -T shellcmd [-c]
./build/st/run_all_tests -T interactive
./build/st/run_all_tests -T shellcmd -l         # list
./build/st/run_all_tests -T shellcmd -f <regex> # filter
```

Unit tests (the `gtests` binary) are folded into the runner as a 4th
type: it parses `gtests --gtest_list_tests`, collapses each parametric
suite to one entry, and treats `DISABLED_` tests as `<manual>` (listed,
run only with `-a`). A unit test runs on the host, so `-T unit` skips
the KVM probe entirely.

**Calibrate timeouts to these numbers.** If a run goes 2-3x over,
something is hung (infinite loop, deadlock, lost wakeup); kill and
investigate rather than extending the timeout.

### Calling kernel `static` functions from gtests: the `STATIC` pattern

`static` blocks gtests from linking; redeclaring the function in the
test file as `extern` works but rots silently when the signature
changes. Project idiom (see `include/tilck/kernel/test/README` and
`include/tilck/common/basic_defs.h`):

- C source uses the `STATIC` macro instead of `static`. It expands
  to `static` in real builds and to nothing under
  `UNIT_TEST_ENVIRONMENT`.
- Declarations live in a dedicated test header
  `include/tilck/kernel/test/<file>.h`, written with the same
  `STATIC` keyword.
- Both the kernel C source AND the gtest `#include` that test
  header. A signature drift breaks the build at one of the two
  sites.

Examples to copy: `kernel/mm/system_mmap.c` +
`include/tilck/kernel/test/mem_regions.h` (called from
`tests/unit/mem_regions.cpp`); `kernel/fork.c` +
`include/tilck/kernel/test/fork.h` (called from
`tests/unit/fork_test.cpp`).

Never `extern`-redeclare a kernel `static` in a `.cpp` test file.

### Keep kernel code untouched when adding tests

**Avoid `#ifdef UNIT_TEST_ENVIRONMENT` in production kernel code.** It
is ugly and erodes the readability of the real implementation. When a
test needs the kernel to behave differently, provide a fake on the
test side instead. Pretty production code matters.

Three mechanisms, in order of preference:

1. **Upgrade a test stub.** If a helper the production code calls
   already has a no-op / NULL-returning stub in
   `tests/unit/generic_stubs.c` / `mm_fakes.cpp` / `misc_fake.c`,
   strengthen the stub to a real fake. Zero kernel changes. Used
   for `hi_vmem_reserve` / `hi_vmem_release` (real malloc fakes in
   `mm_fakes.cpp`) so `alloc_kernel_isolated_stack` works as-is
   in gtests.
2. **Override via `__wrap_*`.** For a kernel function whose body
   you want to replace entirely, add the symbol to
   `other/cmake/wrapped_syms.cmake` and define `__wrap_<symbol>` in
   a test fakes file. Production calls get linker-redirected to the
   wrap. Requires the kernel symbol to be non-`static` (use the
   `STATIC` macro so it stays `static` in real builds).
3. **`STATIC` + test header.** When tests need to *call* a kernel
   function directly (not replace it), use the STATIC + test-header
   pattern documented above.

What you should NOT do: gate kernel branches on
`UNIT_TEST_ENVIRONMENT`, replicate kernel logic in a wrapper, or
re-implement large kernel pieces inside tests. The kernel is the
source of truth; tests sit beside it.

### Portability: target archs vs host archs

Don't confuse the two. **Target arches** (what Tilck cross-compiles
for): i386 (primary), riscv64, x86_64 today; **aarch64 is a planned
future first-class target** and other arches (eg. RISC-V variants)
may follow. **Host arches** (where gtests / build tools run): Linux
x86_64 and aarch64; FreeBSD x86_64 (aarch64 future); Darwin aarch64
only.

Implications for test infrastructure and any header gated on the
host compiler's arch predefines:

- Never gate test-only code on `__aarch64__` alone. gtests are
  built on x86_64 hosts too — code in an `#elif defined(__aarch64__)`
  branch is invisible to them.
- Conversely, never assume "aarch64 = test-only". aarch64 will be a
  real Tilck target; the existing aarch64 test stubs in
  `include/tilck/kernel/hal.h` are gated on
  `__aarch64__ && UNIT_TEST_ENVIRONMENT`, not on `__aarch64__`
  alone, and that pattern is load-bearing.
- Cross-arch test stubs (anything not specific to one arch's
  hardware) belong in a `#ifdef UNIT_TEST_ENVIRONMENT` block placed
  after the arch dispatch — typically end of `hal.h`. The
  arch-specific headers (`common/arch/<arch>/*.h`) should skip
  their production definition of the same symbol under
  `UNIT_TEST_ENVIRONMENT`, so the cross-arch version wins
  uniformly. Pattern in place for IRQ-state primitives
  (`disable_interrupts`, `enable_interrupts`,
  `are_interrupts_enabled`, etc.): production defs in
  `include/tilck/common/arch/{generic_x86,riscv}/*_utils.h` under
  `#ifndef UNIT_TEST_ENVIRONMENT`; test versions in `hal.h` under
  `#ifdef UNIT_TEST_ENVIRONMENT`.

When in doubt: portable test code is gated on
`UNIT_TEST_ENVIRONMENT`, not on a specific arch predefine.

## Fast iteration loop under QEMU (Darwin)

Interactive bootloader + framebuffer is too slow for tight cycles
and awkward to drive over `tmux send-keys`. Use headless text mode +
`-kernel` direct boot + tmux:

```bash
# Configure (once)
./scripts/cmake_run -DBOOT_INTERACTIVE=0 -DMOD_fb=0
make -j

# Run (anything after the script name → qemu-system-i386;
# -append sets kopts: -sercon, -pk, -selftest <name>, -ttys 4)
bash build/run_multiboot_qemu -display curses
bash build/run_multiboot_qemu -display curses -append "-sercon"

# Drive from tmux (boot → prompt in seconds)
tmux new-session -d -s tilck -x 100 -y 35
tmux send-keys -t tilck "bash build/run_multiboot_qemu -display curses" Enter
until tmux capture-pane -t tilck -p | grep -q 'root@tilck:/#'; do sleep 1; done
tmux send-keys -t tilck "<command>" Enter
tmux capture-pane -t tilck:0 -p
```

`run_multiboot_qemu` passes the kernel via `-kernel`, skipping the
Tilck bootloader. `BOOT_INTERACTIVE=0` auto-boots; `MOD_fb=0` keeps
VGA text mode (what `-display curses` renders into tmux).

Second-TTY tests (e.g. tracer on tty1 + victim shell on tty2): user
switches via `Alt-F1`/`Alt-F2`; from tmux send via
`tmux send-keys M-F2`. Alternative: real QEMU window for video +
serial tty for the second session.

`tmux send-keys -l "<text>"` sends literal text (needed for args
starting with `-`); `tmux send-keys "<key>"` sends keys (`Enter`,
`BSpace`, `M-F2`, `C-c`).

## Architecture

```
kernel/           Main kernel code
  arch/           Architecture-specific (i386, x86_64, riscv64, generic_x86)
  fs/             Filesystems (ramfs, vfs, devfs, fat32)
  mm/             Memory management
  tty/            Terminal
  kmalloc/        Heap allocator
modules/          Kernel modules/drivers (acpi, console, fb, kb8042, pci, serial, etc.)
common/           Architecture-independent shared code
boot/             Bootloader (BIOS + UEFI)
include/tilck/    Tilck headers (kernel/, common/ subsystem headers)
userapps/         User-space apps (devshell test runner, etc.)
tests/
  unit/           C++ unit tests (Google Test)
  system/cmds/    System test commands (shellcmds)
  self/           Kernel self-tests
  runners/        Python test infrastructure
scripts/          Build automation (build_toolchain, cmake_run, etc.)
  pkgmgr/         Ruby package manager (see docs/package_manager.md)
  pkgmgr/tests/   Package manager test suite (300+ unit + system tests)
other/cmake/      CMake build modules
toolchain4/       Generated cross-compiler toolchain (not in repo)
```

`build/` artifacts: `tilck` / `tilck_unstripped`, `tilck.img`,
`fatpart`, `gtests`, `st/run_all_tests`, `run_qemu`.

## Toolchain Management

Ruby pkgmgr at `scripts/pkgmgr/`, installed into `toolchain4/`
(per-target-arch). Entry: `./scripts/build_toolchain` (bootstraps
Ruby ≥3.2, execs `pkgmgr/main.rb`).

**For non-trivial pkgmgr work, read `docs/package_manager.md` first**
(authoritative on architecture, three-tier host layout, dependency
resolution, atomic installs, resumable downloads, `-C` reconfigure,
adding packages, the 300+ test suite). This section is only the
bare minimum.

```bash
./scripts/build_toolchain                 # default set for current ARCH
./scripts/build_toolchain -l              # list pkgs + install status
./scripts/build_toolchain -s <pkg>        # install one pkg
./scripts/build_toolchain -u <pkg>        # uninstall one pkg
./scripts/build_toolchain -S <arch>       # install cross-cc for arch
./scripts/build_toolchain -U <arch>       # uninstall cross-cc for arch
./scripts/build_toolchain -C <pkg>        # reconfigure (e.g. busybox)
./scripts/build_toolchain --upgrade       # install new versions after bump
./scripts/build_toolchain -d              # dry-run
./scripts/build_toolchain -t [--coverage] # pkgmgr's own tests
./scripts/build_toolchain --clean         # remove pkgs for current ARCH
./scripts/build_toolchain --clean-all     # remove everything except cache
```

Versions in `other/pkg_versions`. Bump version + `--upgrade` installs
alongside old. CMake detects stale at configure (it's a
`CMAKE_CONFIGURE_DEPENDS`).

Layout:
```
toolchain4/
  cache/                              # tarballs (survive --clean)
  staging/                            # in-progress (atomic install)
  noarch/                             # arch-independent (acpica, gnuefi src)
  gcc-<ver>/<arch>/                   # per-GCC-ver, per-target-arch
  host/<os>-<arch>/
    portable/                         # Tier 1: static, any distro
    <distro>/<pkg>/<ver>/             # Tier 2: distro libc, any host CC
    <distro>/<host-cc>/<pkg>/<ver>/   # Tier 3: C++ ABI-dependent (gtest)
```

Key concepts:
- `Package` base class (`scripts/pkgmgr/package.rb`): every pkg
  inherits + `pkgmgr.register(...)` at load. Key methods:
  `initialize`, `install_impl_internal`, `expected_files`,
  optionally `config_impl` (for `-C`).
- `SourceRef` (`scripts/pkgmgr/source_ref.rb`): decouples upstream
  fetch from pkg def — N pkgs can share one tarball. Example:
  `GNUEFI_SOURCE` → `gnuefi_src` (noarch) + `gnuefi` (arch built).
- `host_tier`: `:portable` / `:distro` / `:compiler` — selects
  which tier a pkg installs into (table in `docs/package_manager.md`).

## FreeBSD Build Host

Supported alongside Linux. Differences:

- **System compiler**: `cc`/`c++` are clang; project uses GCC from
  ports. For host tools (gtest), pass
  `-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++` explicitly.
- **GNU tools**: FreeBSD ships BSD userland. Build prepends
  `scripts/gnu-wrap/` to PATH (redirects `tar`/`make`/`sed` → `gtar`
  /`gmake`/`gsed`). System pkgs in
  `scripts/bash_includes/install_pkgs` (`install_freebsd`), incl.
  many `rubygem-*` that are bundled with Ruby on Linux.
- **gtests**: built with HOST cc, not cross. Shim headers in
  `include/system_headers/` bridge Linux ↔ FreeBSD/macOS (signals,
  errno, clock ids, syscall numbers, dirent, termios). Build
  failure with type conflict / missing constant → fix usually goes
  in a shim.
- **Cross-compile configure scripts**: `--host` alone makes autoconf
  set `cross_compiling=maybe` and try to exec the cross-compiled
  binary (triggers "ELF binary type '0' not known"). Always pass
  both `--host` AND `--build` to force `cross_compiling=yes`. Set
  `BUILD_CC=cc` for pkgs with host-side helpers (ncurses etc.)
  else `BUILD_CC` defaults to `$CC` (cross).
- **Bash shebang**: `#!/usr/bin/env bash`, not `#!/bin/bash`
  (FreeBSD bash is at `/usr/local/bin/bash`).

## Coding Style

**Style matters as much as functionality.** The user rejects a diff
that solves the problem but breaks formatting. Style audit is part
of "done", not polish.

### Workflow before writing C

1. Read `docs/contributing.md` end-to-end (authoritative; summary
   below is a digest, not a substitute).
2. Sample recent kernel files (`kernel/poll.c`, `execve.c`,
   `sched.c`, `exit.c`, `elf.c`) — they reflect current style.
   Older files have legacy drift; emulate the newer ones.
3. After drafting, grep your patterns against `kernel/*.c`. Patterns
   absent from the kernel are probably wrong, even if they compile.
4. **After editing ANY C/H file, run the full style_check sequence
   before committing. NO EXCEPTIONS.**
   ```
   style_check check <files>           # fix all violations
   style_check check --diff <files>    # apply mechanical fixes
   style_check check --gradient --summary <files>  # review prettiness
   ```
   Iterate until zero violations. Apply the tool's `--diff` output
   first; only attempt manual fixes when the tool has no auto-fix.
   If a rule seems wrong, STOP and ask — show the snippet and the
   error. Fixing one rule routinely breaks another (e.g. shortening
   a type name breaks backslash alignment), so the full re-check
   after every edit is non-negotiable.
5. **For cols_80 violations (1-3 chars over):** before wrapping the
   line, exhaust these cheaper fixes: remove optional spaces
   around `+`/`-` in pointer arithmetic (`buf+OFFSET`), use
   compact comma form (`,arg` instead of `, arg`), shorten a
   trailing comment, move `{` to its own line. Only wrap the line
   when none of these recover enough columns.

### Summary of explicit rules (from contributing.md)

- **3 spaces** indentation (not tabs)
- **80 columns** strict line limit (no exceptions).
  **Fixing an over-80-col line is NOT just breaking the line.**
  When a line is part of a columnar pattern (macro tables, aligned
  `#define` blocks, column-aligned call clusters), breaking that
  one line destroys the visual pattern. Instead: reduce the padding
  for the ENTIRE block so all entries stay aligned and fit in 80
  cols. Only when padding reduction can't work (a single field is
  inherently too long) should you consider renaming or refactoring.
  Always look at the surrounding context before touching a long line.
  When reducing padding, do the **minimal reduction necessary** —
  just enough to fit in 80 cols. Don't collapse padding to the
  minimum; the original generous padding was intentional.
- **snake_case** everywhere
- Opening brace on same line for control flow, **new line for
  functions and array initializers**
- Multi-line `if` conditions: opening brace on its own line
- Omit braces for single-statement blocks unless confusing
- Null checks without NULL: `if (ptr)` / `if (!ptr)`
- Long function signatures: return type on previous line, args aligned
- Long function calls: args aligned to opening paren
- Struct init: `*ctx = (struct foo) { .field = val };`
- `#define` values column-aligned
- Comments: `/* ... */` style, multi-line with ` * ` prefix per line
- Add blank line after `if (...) {` / `for (...) {` when the header
  is **longer** than the body's first line (prevents the "hiding"
  effect). When the header is shorter, no blank line — body extends
  past the header anyway.
- Nested `#ifdef` blocks are indented when small in scope

### Additional rules inferred from reading kernel/

Not in `contributing.md`, equally enforced. Skipping any of these
gets the diff rejected.

- **`sizeof(X)` always**, never `sizeof X`. Even for single
  identifiers. Verify: `grep -nE 'sizeof [a-z]' kernel/` returns
  only `(ssize_t)sizeof(...)` casts.
- **Empty loop body: `{ }`, never `;`.** `while (cond) { }` on one
  line. Bare `;` as loop body is rejected. The `{ }` stays on the
  same line as the header when the header fits; never on its own
  line below. Multi-line body only when header itself wraps.
  **Generalization:** avoid empty statements anywhere — labels
  inside lock-scope blocks must go AFTER the closing brace, not as
  `out:;` inside:
  `disable_interrupts(); { ... goto out; ... } out: enable_interrupts();`.
- **Non-const locals at top of block** (pre-C99). Hard rule for any
  mutable local: declare at top of enclosing block (function body,
  lock-scope `{ ... }`, branch arm, loop body). Loop induction in
  `for (int i = ...; ...)` is the exception. Use a bare `{ ... }`
  sub-block to narrow temp scope:
  ```c
  void foo(void)
  {
     int a, b;
     ...
     {
        int temp = compute_thing();
        ... uses temp ...
     }
  }
  ```
  `const` locals are softer: mid-function const computed from in-flight
  state is fine (and often preferred over a forward-declared mutable
  written once); trivially-static const should go at the top.
- **Semantic grouping of statements.** After the declaration block,
  group related statements together and separate groups with blank
  lines. In particular: an assignment and its error check belong
  in the same group (no blank line between them); an ASSERT
  validating a parameter belongs with the code that uses that
  parameter, not isolated between two blank lines. When moving
  declarations to the top of a block, don't let the blank-line-
  after-decls rule break existing semantic groups — tighten the
  groups instead.
  ```c
  /* Good: assignment grouped with its error check,
   * ASSERT grouped with the setup it guards */
  int rc;
  struct termios saved = t->c_term;

  rc = copy_from_user(&t->c_term, argp, sizeof(struct termios));
  if (rc < 0) {
     t->c_term = saved;
     return -EFAULT;
  }
  ```
  ```c
  /* Bad: copy_from_user isolated from its error check */
  rc = copy_from_user(&t->c_term, argp, sizeof(struct termios));

  if (rc < 0) {
  ```
- **No `(void)expr` casts.** Kernel never uses them. Reasons:
  `-Wno-unused-parameter` is on (unused params don't warn); musl on
  i386 doesn't `warn_unused_result` on `read`/`write`/`getpid`/...
  (discarded syscall returns don't warn).
- **Unused callback params: name `unused`**, not `_ctx`/`_param`. Refs:
  `kernel/sched.c:idle`, `main.c:do_async_init`,
  `datetime.c:clock_drift_adj`.
- **One statement per line.** Don't pack `close(a); close(b);` on
  one line.
- **Static fn defs: type/modifiers on their own line** — uniformly,
  even for short `static void idle(void *unused)`. (`contributing.md`
  documents this only for long signatures; kernel applies it always.
  See `kernel/poll.c`, `execve.c`.)
- **`{` placement** depends on context:
  - **Same line**: control flow w/ single-line condition (`if (x) {`).
  - **Own line**: function bodies (always), array initializers,
    multi-line `if` conditions where `)` is at the wrapped line's end
    (see `kernel/elf.c` ELF header check), lock-scope blocks
    (`disable_preemption(); { ... } enable_preemption();` —
    `kernel/sched.c`, `execve.c`).
- **Em-dashes in comments accepted.** Use where prose flows; don't
  sprinkle. Examples in recent dp/tracer code, `kernel/poll.c`.

### Multi-line call / declaration syntax (paren symmetry)

Multi-line calls/decls must be visually symmetric. Two valid styles;
the asymmetric hybrid is never valid.

**Style 1 — aligned to opening paren.** Use when the opening
expression leaves room for the first arg on the same line. Args
sit under the first; `;` at end of last arg.
```c
very_long_expression(arg1,
                     arg2,
                     arg3);
```

**Style 2 — indented continuation, `);` on own line.** Use when
style 1 doesn't fit. Open `(` ends the first line; args at +3
indent; closing `);` on its own line, aligned with the wrapping
statement.
```c
context_switch = asm_save_regs_and_schedule(
   __builtin_extract_return_addr(__builtin_return_address(0))
);
```

**Style 3 — NEVER.** Args indented but `);` at end of last arg —
top-heavy, no closing-paren symmetry:
```c
this_is_a_long_expression(
   arg, arg, arg);    /* WRONG */
```

**Cross-function uniformity.** When related calls/defs cluster on
screen (wrapper inlines, setup calls, typedef columns), pick one
style for the whole cluster. If any entry needs style 2, use
style 2 for all entries — even those that could fit style 1.

Same for column-aligned multi-line declarations (struct
initializers, `typedef` lists, `#define` blocks): pad so trailing
columns (`;`, `}`, `=`) land at the same column. Pretty > compact.

### Multi-line control-flow conditions

When an `if`/`while`/`for` condition spans multiple lines: body
MUST be braced, `{` on its OWN line aligned with the keyword. Holds
even for single-statement body (overrides the "omit braces" rule).

```c
if (this + is + a + very + long +
    multi + line + expression)
{
   do_something();
}
```

Never:
```c
if (cond +                          /* WRONG: { on cond line */
    more) {
   ...
}
if (cond +                          /* WRONG: brace-less w/ wrapped cond */
    more)
   do_something();
```

Reason: with a wrapped condition, a bare body statement is hard to
tell from the next statement at the same indent.

**Nested-if propagation**: when a nested `if` is in brace-on-own-line
form, the outer `if` containing it MUST also use braces — its body
spans multiple visual lines.

**Brace-on-own-line is the fall-back.** Before using it, try to
flatten the condition. Three alternatives, picked on context:

```c
/* (a) extract sub-expressions to named locals — best when 1-2 exprs */
if (curr_state == TASK_STATE_RUNNING) {
   const u64 curr_vruntime = atomic_load(&curr->ticks.vruntime);
   const u64 selected_vruntime = atomic_load(&selected->ticks.vruntime);
   if (curr_vruntime < selected_vruntime)
      selected = curr;
}

/* (b) convenience pointer one level up — best when block reads
 * multiple fields of the same nested struct */
struct sched_ticks *const ct = &curr->ticks;
struct sched_ticks *const st = &selected->ticks;
if (atomic_load(&ct->vruntime) < atomic_load(&st->vruntime))
   selected = curr;

/* (c) accept brace-on-own-line — when factoring just rearranges ugliness */
```

Don't over-extract: a wall of `const u64 x = ...;` is noise. Storing
every leaf as a local is noise; a pointer one level up usually wins.

**Naming locals**: descriptive over abbreviated.
`pos_vruntime < selected_vruntime` reads like prose. When 80 cols
forces shorter, fall-back ladder:
1. Mildly abbreviate the modifier, keep the noun (`selected_vruntime`
   → `sel_vruntime`).
2. Drop `const` from BOTH statements for symmetry (never one const
   and one not).
3. Last resort: short names (`pos_vr`/`sel_vr`).

Never break the assignment statement itself as the fall-back —
broken assignment costs more readability than the const-drop or
abbreviation.

### Reference files to consult

| Pattern | Look at |
|---------|---------|
| Static fn defs + multi-arg alignment | `kernel/poll.c`, `kernel/execve.c` |
| Lock-scope `{ ... }` blocks | `kernel/sched.c`, `kernel/execve.c` |
| Multi-line `if` with brace-on-own-line | `kernel/elf.c` (`e_ident` validation) |
| Struct init with designators | `kernel/sched.c` (`create_pid_visit_ctx`) |
| Userspace static fn style | `userapps/tracer/screen_tracing.c`, `userapps/dp/dp_main.c` |
| Comment header for a non-trivial file | the prologues of any of the above |

## Cross-config and cross-arch pitfalls

Default local build (`gcc` + `DEBUG_CHECKS=1` + `KRN_HANG_DETECTION=0`
+ i386) hides bugs that CI catches. Before declaring done, run one
release + UBSAN config and one riscv64 build:

```bash
CMAKE_ARGS='-DKERNEL_UBSAN=1 -DKRN_HANG_DETECTION=1 -DKRN_MINIMAL_TIME_SLICE=1' \
   ./scripts/build_generators/gcc_rel && make -j
CMAKE_ARGS='-DARCH=riscv64 -DKERNEL_UBSAN=1 -DKRN_HANG_DETECTION=1 -DKRN_MINIMAL_TIME_SLICE=1' \
   ./scripts/build_generators/gcc     && make -j
```

Full sweep: `./scripts/adv/gen_other_builds`. CI flags layered on
i386/riscv64 (`other/ci/{i386,riscv64}.yml`): `KERNEL_UBSAN`,
`KRN_HANG_DETECTION`, `KRN_MINIMAL_TIME_SLICE`,
`KRN_RESCHED_ENABLE_PREEMPT`, `TIMER_HZ=500`,
`KRN_KMALLOC_FREE_MEM_POISONING`. Matrix includes release configs
with `DEBUG_CHECKS=0`.

**Code under feature flags** (`#if KRN_HANG_DETECTION`, etc.) is
invisible to the default build. If touching such a block, set the
flag before building.

**Macros that may expand to nothing.** `ASSERT()`, `ASSERT_TASK_STATE()`,
`ASSERT_CURR_TASK_STATE()` become `do {} while(0)` under
`DEBUG_CHECKS=0`. Brace-less `if (x) ASSERT(...)` then trips
`-Werror=empty-body` in release builds. Always brace-wrap.

**Debug-only code: prefer `if (DEBUG_CHECKS) { ... }`.** `DEBUG_CHECKS`
is a literal 0/1 macro, so the compiler dead-code-eliminates the dead
branch — release builds are identical to ones that never had the
block, and the source has no conditional-compilation noise. Use this
whenever the debug-only code consists only of statements:

```c
if (DEBUG_CHECKS) {
   verify_invariant(thing);
   trace_log("foo");
}
```

When the code requires a debug-only *declaration* (e.g. a local whose
only uses are inside `ASSERT`s), `if (DEBUG_CHECKS)` stops working —
the decl is visible in both branches and `-Werror=unused-variable`
breaks the release build. Use `DEBUG_ONLY(decl)` there. It is a
necessary evil, not a preferred form: don't reach for it when the
plain `if (DEBUG_CHECKS)` form fits.

```c
DEBUG_ONLY(enum task_state state = atomic_load(&ti->state));
ASSERT(state != TASK_STATE_ZOMBIE);
ASSERT(state == TASK_STATE_RUNNING || state == TASK_STATE_RUNNABLE);
```

**A whole debug-only function: guard the body with `if (!DEBUG_CHECKS)`,
don't wrap the function in `#if`.** A selftest or helper that only makes
sense under debug checks should bail at the top, not vanish behind a
preprocessor block — the function stays compiled and type-checked in
release while its body dead-code-eliminates:

```c
void selftest_cow_oom(void)
{
   if (!DEBUG_CHECKS) {
      printk("cow_oom: needs DEBUG_CHECKS=1\n");
      se_regular_end();
      return;
   }
   ... real test, freely using debug-only hooks ...
}
```

**Debug-only state shared across translation units: a `static` + a
setter, never an `extern` under `#if`.** Keep the storage `static` (with
the hot-path check written `if (DEBUG_CHECKS && the_static && ...)`) and
expose an always-declared setter whose body is itself `if (DEBUG_CHECKS)`-
gated. No `#if`, no exported variable; all of it dead-code-eliminates in
release. The `if (DEBUG_CHECKS && the_static)` reference counts as a *use*,
so the `static` does not trip `-Werror=unused-variable` (same reason the
`if (DEBUG_CHECKS) { use(x); }` form is warning-free — unlike `ASSERT`,
which removes its argument). Pattern in `kernel/kmalloc/kmalloc.c`
(`kmalloc_inject_fail_next` + `debug_kmalloc_inject_fail_next()`):

```c
/* kmalloc.c */
static bool kmalloc_inject_fail_next;
void debug_kmalloc_inject_fail_next(void)
{
   if (DEBUG_CHECKS)
      kmalloc_inject_fail_next = true;
}
/* general_kmalloc(), hot path: */
if (DEBUG_CHECKS && kmalloc_inject_fail_next && *size >= PAGE_SIZE) { ... }
/* kmalloc.h: just `void debug_kmalloc_inject_fail_next(void);` */
```

`#ifdef DEBUG_CHECKS ... #endif` only when neither form fits
(debug-only struct fields, debug-only header includes). **Do not use
`IS_RELEASE_BUILD` as a debug gate — `DEBUG_CHECKS` is the canonical
macro for this.**

**Strict-alignment types propagate.** A struct embedding `atomic_u64_t`
/ `atomic_s64_t` (anything `ALIGNED_AT(8)`) inherits 8-byte alignment
recursively. Intrusive `list_for_each` produces a sentinel
`container_of(&head, struct task, ...)` whose value doesn't satisfy
that alignment even though the underlying memory access is sound.
`-fsanitize=alignment` (on in i386/riscv64 UBSAN builds) fires on
`&pos->member` of the sentinel. Mitigation lives in
`include/tilck/kernel/list.h`: `list_node_ptr_of()` routes through
`(char *)pos + offsetof(...)` cast back via `TO_PTR()`. New list
macros must follow the same pattern.

**Cast-align.** riscv64 is strict; `(char *)x → (T *)` trips
`-Werror=cast-align`. Bypass via `TO_PTR(x)` (preferred for
integer-shaped values, idiomatic across the kernel) or
`(T *)(void *)x` (pointer-to-pointer). `(T *)(char *)x` is broken on
riscv64. Never `(void)expr` to discard a `warn_unused_result` return
— check it and act.

**Integer narrowing.** `-Wconversion` / `-Wshorten-64-to-32` are on
under `--contrib` and parts of CI. Explicit cast on the value being
narrowed. timespec / size_t→int conversions are the common offenders;
pattern in `kernel/fs/fs_syscalls.c` (explicit cast + comment on the
assumption).

**WEAK fallbacks in selftests.** `kernel/misc.c` carries WEAK stubs
returning false / no-op for arch-absent features (e.g.
`rtc_wait_for_second_edge` on riscv64). A selftest that calls into
such an API must probe + skip on the first call; otherwise it hangs
on a signal that never arrives until the per-test timeout fires.
Canonical pattern: `tests/self/se_rtc_uie.c`.

## Porting expectations

Porting (kernel→userspace, framework A→B, lang A→B, etc.) requires
**feature parity AND visual fidelity** — every keybinding, menu,
color, modal flow, interaction. MVP-grade "covers the main features"
gets rejected as a regression.

Workflow:

1. **Read the original exhaustively first.** Walk every source file,
   list every key, menu, non-obvious code path. Save as spec
   (`docs/<thing>-feature-spec.md`) — survives the conversation,
   serves as a reviewable checklist.
2. **Boot the original.** Walk every UI path, capture transcripts
   for later diff.
3. **Track the gap explicitly.** Append a "gap list" to the spec;
   tick items as closed. User wants the list at zero before done.
4. **Reuse logic.** If original is mostly data-flow (metadata-driven
   rendering, etc.), keep the code intact and change only the
   output target. Keeps byte-identical results, avoids a metadata
   mirror that drifts.
5. **Diff at the end.** Both old and new in the same harness, capture
   each screen, `diff`. Any non-runtime difference is a regression.

User pointing out a missing feature = failure of step 1; pointing
out a visual divergence = failure of step 5.

## Commit Style
Each commit must be self-contained, compile in all configs, and pass all tests
(critical for `git bisect`).

Keeping the series bisect-safe sometimes means folding a fix into the earlier
commit that introduced a bug, rather than appending a "fixup". `git rebase -i`
IS usable here: the harness's "interactive flags (`-i`) not supported" note only
refers to the default editor-driven flow (which hangs waiting for an editor).
Drive it non-interactively instead, with `GIT_SEQUENCE_EDITOR` (rewrites the
rebase to-do: `pick`→`edit`/`reword`/`squash`) and `GIT_EDITOR` (supplies
messages). To edit the commit that introduced a bug:

```bash
GIT_SEQUENCE_EDITOR='sed -i 1s/^pick/edit/' git rebase -i <bug-commit>~1
#  ...fix the files, then:  git add -A && git commit --amend --no-edit
GIT_EDITOR=true git rebase --continue
```

`GIT_EDITOR=true` accepts existing messages unchanged (use `--amend -F file` /
`--no-edit` to set them). The same pattern unblocks any editor-driven git
command (e.g. `git commit` without `-m`). Verified working in this environment.

## No changes without testing

Never commit changes affecting build logic, package installs, or
runtime behavior without exercising the affected path.

- **pkgmgr unit tests** (`./scripts/build_toolchain -t`) use
  `FakePackage` stubs — passing proves logic is sound, NOT that real
  installs work.
- **pkgmgr install changes**: `./scripts/build_toolchain -s <pkg>`
  + check `expected_files` are actually produced.
- **Multi-arch**: repeat with `-a <arch>` per arch, or
  `--system-tests -a ALL`.
- **Dep-graph changes**: `--deps <pkg>` to confirm tree, then
  force-reinstall a dependent to check ordering.
- **`config_impl` / interactive flows**: verify the host tool binary
  links against the expected libs (`ldd`, `strings`, `V=1` build
  logs).

If a change is genuinely untestable in this environment (e.g. needs
a real TTY), say so and ask for an exception rather than committing
blind.

## CI
Azure DevOps Pipelines tests all commits across i386, riscv64, x86_64 with
debug/release builds, unit tests, system tests, and coverage.
Status: https://dev.azure.com/vkvaltchev/Tilck
