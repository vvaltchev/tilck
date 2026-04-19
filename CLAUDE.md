# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository.

## Project Overview

Tilck is an educational monolithic kernel designed to be Linux-compatible at
binary level. It runs on i386 (primary), riscv64, and x86_64. It implements
~100 Linux syscalls and runs mainstream Linux programs (BusyBox, Vim, TinyCC,
Micropython, Lua) without custom rewrites. ~13,300 lines of kernel code.
Licensed BSD 2-Clause.

## Working with Git History

**Always check `docs/annotated-commit-history.txt` before reading actual
diffs.** Any task that requires digging into the project's history —
understanding why a subsystem was introduced, when a refactor happened, what
a commit was trying to fix, where a feature came from — should consult the
annotated file *first*, and fall through to `git log` / `git show` /
`git blame` only if the annotation is too shallow. The purpose of this
file is to build context *fast*: skim the relevant arc, and only pay the
cost of reading the actual diff for the specific commits that matter.

The file is a plain-text, 80-col, ASCII walk of every first-parent commit
on master (~4,900 commits, March 2016 through today). Each entry has its
verbatim git log header plus an "AI notes:" section that summarizes what
changed mechanically from the diff and, when non-obvious, flags inferred
intent as a guess. Large refactors and PR-merge branches are grouped into
multi-commit arcs so broad themes show up at a glance. Read it with
`less docs/annotated-commit-history.txt`; pattern-search (`/`) for
subsystem names, commit hashes, or specific terms.

`scripts/dev/claude/annotate_commits` is the helper used to build and
extend this file — use it when adding new annotations. It wraps git log /
show / meta-range / merges / merge-log / append behind sub-commands so the
whole workflow sits behind a single permission pattern.

## Your Dedicated Tools Directory: `scripts/dev/claude/`

`scripts/dev/claude/` is *your* directory — a dedicated home for helper
scripts that Claude Code writes to make its own work more efficient. When
a task needs many related shell operations (git plumbing, file scans,
repeated pipelines, mechanical tooling), write a small dispatcher script
under `scripts/dev/claude/` with sub-commands instead of running each
underlying command one at a time. The user grants permission once for
`Bash(scripts/dev/claude/<tool>:*)` rather than approving each git / sed /
awk / find invocation, and the resulting workflow is auditable afterwards
by reading the script itself.

If you find yourself about to emit a long sequence of shell commands the
user would have to approve individually, stop and write the helper here
first.

`scripts/dev/claude/annotate_commits` is the canonical example — a Bash
dispatcher with sub-commands (regen-list, meta-range, merges, merge-log,
append, show-head, sha-meta). Follow the same pattern for new tools:
argument-parsed sub-commands, single permission pattern, dev-only (never
called from the build system).

## Build Commands

### First-time setup (for each target architecture)
```bash
export ARCH=i386                         # Target arch. One of: i386, riscv64, x86_64
./scripts/build_toolchain                # Build cross-compiler toolchain (one-time)
./scripts/build_toolchain -s host_gtest  # Also install unit test deps
./scripts/build_toolchain -l             # List available packages
./scripts/build_toolchain -h             # Show the help.
```

### CMake configuration
When running in the root directory, the cmake_run wrapper script can be used
to run cmake. It does some checks and forwards most of its arguments to cmake.
Example uses:
```bash
./scripts/cmake_run                          # Default debug build (ARCH=i386)
./scripts/cmake_run -DRELEASE=1              # Release build (-O3)
./scripts/cmake_run -DDEBUG_CHECKS=0         # Disable debug checks
./scripts/cmake_run -DARCH=riscv64           # Target RISC-V
./scripts/cmake_run -DARCH=x86_64            # Target x86_64

./scripts/cmake_run --contrib                # Configure the project for
                                             # developers / contributors.
                                             # Uses extra stress options,
                                             # clang for the C files in order
                                             # to have -Wconversion etc.
```


### Build (basic)
```bash
make                    # Build the project one file at a time in the build/
                        # directory. Good for debugging build errors.
                        # Runs cmake_run automatically if needed.

make -j                 # Parallel build. Don't use -j$(nproc) please.
make gtests             # Build unit tests (requires gtest/gmock)
```

### Rebuild image only (skip recompilation)
```bash
make rem                # Deletes fatpart + tilck.img, then rebuilds
```

### Out-of-tree builds
```bash
mkdir ~/builds/tilck01 && cd ~/builds/tilck01
/path/to/tilck/scripts/cmake_run             # Configure from any directory
make -j                                      # Build there
```

### Extensive build validation (slow!)

```bash
./scripts/adv/gen_other_builds  # Build Tilck in all the configurations in
                                # scripts/build_generators/ in the other_builds/
                                # directory. Very useful for making sure that
                                # at least for the given configuration:
                                # { ARCH, HOST_ARCH, GCC_TC_VER } we're not
                                # breaking the build.
```


## Testing

Four test types exist:

```bash
# Unit tests (Google Test, runs on host, requires gtest/gmock)
./build/gtests                               # Run all (161 tests, ~2s)
./build/gtests --gtest_filter=kmalloc_test.* # Run one test suite
./build/gtests --gtest_list_tests            # List all test suites & cases

# All tests via test runner (boots QEMU VMs, requires KVM)
./build/st/run_all_tests -c                  # All tests, single VM
./build/st/run_all_tests                     # All tests, separate VMs per test

# By type (-T accepts minimal prefixes: 'se' = selftest, 'sh' = shellcmd)
./build/st/run_all_tests -T selftest         # Kernel self-tests
./build/st/run_all_tests -T shellcmd         # System tests (syscall-based)
./build/st/run_all_tests -T shellcmd -c      # System tests, single VM
./build/st/run_all_tests -T interactive      # Interactive tests (needs --intr build)

# Filtering and listing
./build/st/run_all_tests -T shellcmd -l      # List available tests
./build/st/run_all_tests -T shellcmd -f fork # Run tests matching regex
./build/st/run_all_tests -T selftest -f kcond # Run a single self-test
```

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

Key build artifacts in `build/`: `tilck` (kernel), `tilck_unstripped`,
`tilck.img` (bootable image), `fatpart` (FAT32 initrd), `gtests` (unit tests),
`st/run_all_tests` (test runner), `run_qemu`.

## Toolchain Management

The toolchain is managed by a Ruby package manager at `scripts/pkgmgr/`,
installed into `toolchain4/` (per-target-architecture subtrees). The entry
point is `./scripts/build_toolchain`, which bootstraps Ruby (>= 3.2,
auto-downloaded if needed) and execs into `scripts/pkgmgr/main.rb`.

**For any non-trivial pkgmgr work, read `docs/package_manager.md` first.**
That document is the authoritative overview — it covers architecture, the
three-tier host layout, dependency resolution, atomic installs, resumable
downloads, the `-C <pkg>` reconfiguration flow, how to add a new package,
and the 300+ test suite. This CLAUDE.md section only surfaces the bare
minimum needed when pkgmgr is incidental to another task.

### Quick CLI reference

```bash
./scripts/build_toolchain                 # Install default set for current ARCH
./scripts/build_toolchain -l              # List packages and install status
./scripts/build_toolchain -s <pkg>        # Install a specific package
./scripts/build_toolchain -u <pkg>        # Uninstall a specific package
./scripts/build_toolchain -S <arch>       # Install cross-compiler for an arch
./scripts/build_toolchain -U <arch>       # Uninstall cross-compiler for an arch
./scripts/build_toolchain -C <pkg>        # Reconfigure a package (e.g. busybox)
./scripts/build_toolchain --upgrade       # Install new versions after a bump
./scripts/build_toolchain -d              # Dry-run (preview actions)
./scripts/build_toolchain -t              # Run pkgmgr's own test suite
./scripts/build_toolchain -t --coverage   # Unit tests with coverage report
./scripts/build_toolchain --clean         # Remove all pkgs for current ARCH
./scripts/build_toolchain --clean-all     # Remove everything except cache
```

Package versions are declared in `other/pkg_versions`. Changing a version
there and running `./scripts/build_toolchain --upgrade` installs the new
version alongside the old. CMake detects stale packages at configure time
(`other/pkg_versions` is a `CMAKE_CONFIGURE_DEPENDS`).

### Toolchain layout (summary — see docs/package_manager.md for full picture)

```
toolchain4/
  cache/                              # Downloaded tarballs (survive --clean)
  staging/                            # In-progress builds (atomic install)
  noarch/                             # Arch-independent (acpica, gnuefi src)
  gcc-<ver>/<arch>/                   # Per-GCC-version, per-target-arch
  host/<os>-<arch>/
    portable/                         # Tier 1: static, any distro
    <distro>/<pkg>/<ver>/             # Tier 2: distro libc, any host CC
    <distro>/<host-cc>/<pkg>/<ver>/   # Tier 3: C++ ABI-dependent (gtest)
```

### Key concepts worth knowing before reading code

- **`Package` base class** (`scripts/pkgmgr/package.rb`): every package is a
  Ruby class inheriting from this. Registers itself via
  `pkgmgr.register(...)` at file-load time. Key methods: `initialize`,
  `install_impl_internal`, `expected_files`, optionally `config_impl`
  (for `-C` reconfiguration).
- **`SourceRef`** (`scripts/pkgmgr/source_ref.rb`): decouples upstream
  source fetch from package definition. Multiple `Package` classes can
  share one `SourceRef` — the tarball is cached once, consumed N times.
  Canonical example: `GNUEFI_SOURCE` backs both `gnuefi_src` (noarch
  headers) and `gnuefi` (arch-specific built libs) in `gnuefi.rb`.
- **`host_tier`** (`:portable` / `:distro` / `:compiler`) selects which
  of the three host tiers a package installs into — see the table in
  `docs/package_manager.md`.

## FreeBSD Build Host

FreeBSD is a supported build host alongside Linux. Key differences:

- **System compiler**: `cc`/`c++` are clang on FreeBSD, but the project
  uses GCC from ports. When invoking cmake for host tools (e.g. gtest),
  pass `-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++` explicitly.

- **GNU tools**: FreeBSD ships BSD userland, not GNU. The build system
  prepends `scripts/gnu-wrap/` to PATH, which contains wrappers that
  redirect `tar`, `make`, `sed`, etc. to their GNU equivalents (`gtar`,
  `gmake`, `gsed`). FreeBSD system packages for these are listed in
  `scripts/bash_includes/install_pkgs` (`install_freebsd` function),
  including many `rubygem-*` packages that are bundled with Ruby on
  Linux but separate on FreeBSD.

- **Unit tests (gtests)**: Compiled with the HOST compiler, not the
  cross-compiler. The `include/system_headers/` directory contains shim
  headers that bridge differences between Linux and FreeBSD/macOS system
  headers (signal types, errno values, clock IDs, syscall numbers,
  dirent, termios flags). When a gtest build fails on FreeBSD with a
  type conflict or missing constant, the fix usually goes in a shim.

- **Cross-compilation configure scripts**: On FreeBSD, passing only
  `--host` to autoconf makes it set `cross_compiling=maybe` and try to
  exec a cross-compiled binary, which triggers the kernel's uprintf
  "ELF binary type '0' not known." to the terminal. Always pass both
  `--host` AND `--build` so configure sets `cross_compiling=yes`
  directly. Also set `BUILD_CC=cc` for packages that compile host-side
  helper tools (ncurses, etc.), otherwise `BUILD_CC` defaults to `$CC`
  (the cross-compiler).

- **Bash shebang**: Use `#!/usr/bin/env bash`, not `#!/bin/bash`
  (bash is at `/usr/local/bin/bash` on FreeBSD).

## Coding Style

- **3 spaces** indentation (not tabs)
- **80 columns** strict line limit (no exceptions)
- **snake_case** everywhere
- Opening brace on same line for control flow, **new line for functions and array initializers**
- Multi-line `if` conditions: opening brace on its own line
- Omit braces for single-statement blocks unless confusing
- Null checks without NULL: `if (ptr)` / `if (!ptr)`
- Long function signatures: return type on previous line, args aligned
- Long function calls: args aligned to opening paren
- Struct init: `*ctx = (struct foo) { .field = val };`
- `#define` values column-aligned
- Comments: `/* ... */` style, multi-line with ` * ` prefix per line
- Add blank line after `if (...) {` / `for (...) {` when the header is long relative to the body's first line (prevents "hiding" effect)
- Nested `#ifdef` blocks are indented when small in scope

## Commit Style
Each commit must be self-contained, compile in all configs, and pass all tests
(critical for `git bisect`)

## No changes without testing
Never commit changes that affect build logic, package installs, or runtime
behavior without actually exercising the affected path. The pkgmgr unit test
suite (`./scripts/build_toolchain -t`) uses `FakePackage` stubs with no real
downloads or builds — passing it proves pkgmgr logic is sound, NOT that a real
package installs correctly. For pkgmgr changes, run
`./scripts/build_toolchain -s <pkg>` (or the relevant install) and check
that `expected_files` are actually produced. For multi-arch changes, repeat
with `-a <arch>` on each affected arch, or run `--system-tests -a ALL`. For
dep-graph changes, run `--deps <pkg>` and confirm the tree, then
force-reinstall a dependent to check ordering. For `config_impl` /
interactive flows, at minimum verify the host tool binary links against the
expected libraries (`ldd`, `strings`, or `V=1` build logs showing the right
`-I`/`-L` paths). If a change is genuinely untestable in the current
environment (e.g. requires a real TTY), say so explicitly and ask for an
exception rather than committing blind.

## CI
Azure DevOps Pipelines tests all commits across i386, riscv64, x86_64 with
debug/release builds, unit tests, system tests, and coverage.
Status: https://dev.azure.com/vkvaltchev/Tilck
