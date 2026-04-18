# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository.

## Project Overview

Tilck is an educational monolithic kernel designed to be Linux-compatible at
binary level. It runs on i386 (primary), riscv64, and x86_64. It implements
~100 Linux syscalls and runs mainstream Linux programs (BusyBox, Vim, TinyCC,
Micropython, Lua) without custom rewrites. ~13,300 lines of kernel code.
Licensed BSD 2-Clause.

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
  pkgmgr/         Ruby package manager (exp-ruby branch)
  pkgmgr/tests/   Package manager test suite (293 unit + system tests)
other/cmake/      CMake build modules
toolchain4/       Generated cross-compiler toolchain (not in repo; toolchain3/ on master)
```

Key build artifacts in `build/`: `tilck` (kernel), `tilck_unstripped`,
`tilck.img` (bootable image), `fatpart` (FAT32 initrd), `gtests` (unit tests),
`st/run_all_tests` (test runner), `run_qemu`.

## Toolchain Management

The toolchain lives in `toolchain3/` and is managed per-architecture.

**On `master` (Bash package manager):**
```bash
./scripts/build_toolchain -l              # List all packages and install status
./scripts/build_toolchain -s <pkg>        # Install a specific package
./scripts/build_toolchain -d <pkg>        # Uninstall a specific package
./scripts/build_toolchain --clean         # Remove all pkgs for current ARCH
./scripts/build_toolchain -a --clean      # Remove all pkgs for ALL archs
./scripts/build_toolchain --clean-all     # Remove everything except cache
```

**On `exp-ruby` branch (Ruby package manager):** The `exp-ruby` branch is
reimplementing the toolchain/package management in Ruby. The entry point is
the same (`./scripts/build_toolchain`) but the Bash bootstrap now sets up
Ruby (>= 3.2, auto-downloaded if needed) and then `exec`s into
`scripts/pkgmgr/main.rb`. Key CLI differences:
```bash
./scripts/build_toolchain -l              # Same: list packages
./scripts/build_toolchain -s <pkg>        # Same: install
./scripts/build_toolchain -u <pkg>        # Uninstall (was -d on master)
./scripts/build_toolchain -S <arch>       # Install compiler for a specific arch
./scripts/build_toolchain -U <arch>       # Uninstall compiler for a specific arch
./scripts/build_toolchain -d              # Dry-run (was not available on master)
./scripts/build_toolchain -c ALL -l       # List packages across all compilers
./scripts/build_toolchain -g arch -l      # Group listed packages by arch
```

**How to detect which package manager is active:** Check for the directory
`scripts/pkgmgr/`. If it exists, you are on the Ruby package manager branch.
If not, you are on master with the Bash package manager.

**Migration status on `exp-ruby`:** All packages have been ported to Ruby
(in `scripts/pkgmgr/*.rb`). The old Bash package scripts (`scripts/tc/pkgs/`)
no longer exist on this branch. The Bash package scripts on master are still
the **reference implementation** — use them to understand the original build
logic when debugging Ruby package definitions.

**Ruby package structure:** Each Ruby package is a class inheriting from
`Package` (defined in `scripts/pkgmgr/package.rb`). It registers itself
with `pkgmgr.register(MyPackage.new())` at file load time. Key methods to
implement: `initialize` (name, url, arch_list, deps), `install_impl_internal`
(build logic), `expected_files` (validation). The `PackageManager` singleton
(`scripts/pkgmgr/package_manager.rb`) handles discovery, install/uninstall
orchestration, and status reporting.

Toolchain directory layout:

On `master` the toolchain root is `toolchain3/`; on `exp-ruby` it is
`toolchain4/` (the Ruby pkgmgr uses a different directory to avoid
conflicts when switching branches).

```
toolchain4/                         # (toolchain3/ on master)
  cache/                            # Downloaded tarballs (preserved across cleans)
  noarch/                           # Arch-independent packages (acpica, gtest src)
  gcc-<VER>/                        # Per-GCC-version cross-compiled packages
    <arch>/                         # Per-target-arch (i386, x86_64, riscv64)
  host/
    <os>-<host_arch>/               # e.g. freebsd-x86_64, linux-x86_64
      portable/                     # Tier 1: static, any distro (cross-compilers)
      <distro>/                     # e.g. freebsd-15.0, ubuntu-22.04
        <pkg>/<ver>/                # Tier 2: links distro libc, any CC (mtools)
        ruby/<ver>/                 # Bootstrap Ruby (not a registered package)
        <host-cc>/                  # e.g. gcc-14.2.0
          <pkg>/<ver>/              # Tier 3: depends on host CC C++ ABI (gtest)
```

Packages are downloaded to `cache/` and extracted/built into the appropriate
directory. Downloaded tarballs survive `--clean` but not `--clean-all`.

**Ruby pkgmgr test suite (exp-ruby only):**
```bash
./scripts/build_toolchain -t                       # Run 293 unit tests
./scripts/build_toolchain -t --system-tests        # + install all pkgs, build all archs
./scripts/build_toolchain -t --system-tests -a i386  # Single arch (default: current)
./scripts/build_toolchain -t --system-tests -a ALL   # All archs (i386, x86_64, riscv64)
./scripts/build_toolchain -t --system-tests --test-packages-filter ncurses  # Filter optional pkgs
./scripts/build_toolchain -t --system-tests --all-build-types   # Also test all cmake configs
./scripts/build_toolchain -t --system-tests --run-also-tilck-tests  # Also run gtests + system tests
./scripts/build_toolchain -t -F <regex>            # Filter unit tests by name
./scripts/build_toolchain -t --coverage            # Unit tests with code coverage report
```

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

## CI
Azure DevOps Pipelines tests all commits across i386, riscv64, x86_64 with
debug/release builds, unit tests, system tests, and coverage.
Status: https://dev.azure.com/vkvaltchev/Tilck
