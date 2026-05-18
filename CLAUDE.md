# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository.

## Project Overview

Tilck is an educational monolithic kernel designed to be Linux-compatible at
binary level. It runs on i386 (primary), riscv64, and x86_64. It implements
~100 Linux syscalls and runs mainstream Linux programs (BusyBox, Vim, TinyCC,
Micropython, Lua) without custom rewrites. ~13,300 lines of kernel code.
Licensed BSD 2-Clause.

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


## Testing

Four test types: unit (gtest, host), selftest (in-kernel), shellcmd
(syscall-based), interactive (`--intr` build).

```bash
# Unit tests (host, needs gtest/gmock)
./build/gtests
./build/gtests --gtest_filter=kmalloc_test.*
./build/gtests --gtest_list_tests

# Test runner (boots QEMU, needs KVM)
./build/st/run_all_tests             # one VM per test
./build/st/run_all_tests -c          # single VM

# By type (-T prefix-matched: 'se'=selftest, 'sh'=shellcmd)
./build/st/run_all_tests -T selftest
./build/st/run_all_tests -T shellcmd [-c]
./build/st/run_all_tests -T interactive
./build/st/run_all_tests -T shellcmd -l         # list
./build/st/run_all_tests -T shellcmd -f <regex> # filter
```

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

### Summary of explicit rules (from contributing.md)

- **3 spaces** indentation (not tabs)
- **80 columns** strict line limit (no exceptions)
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
(critical for `git bisect`)

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
