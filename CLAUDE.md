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
./scripts/build_toolchain -s gtest_src host_gtest  # Also install unit test deps
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
other/cmake/      CMake build modules
toolchain3/       Generated cross-compiler toolchain (not in repo)
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

**Migration status on `exp-ruby`:** Packages ported to Ruby (in
`scripts/pkgmgr/*.rb`): gcc, acpica, zlib, mtools, busybox, gnuefi, gtest.
Packages still in Bash only (in `scripts/tc/pkgs/`): lcov, cmake, dtc,
fbdoom, lua, micropython, ncurses, vim, treecmd, tfblib, libmusl, tcc.
When adding a new package or modifying an existing one, check which system
it lives in. The Bash package scripts on master (`scripts/tc/pkgs/`) are
the **reference implementation** — use them to understand the build logic
when porting or debugging Ruby package definitions.

**Ruby package structure:** Each Ruby package is a class inheriting from
`Package` (defined in `scripts/pkgmgr/package.rb`). It registers itself
with `pkgmgr.register(MyPackage.new())` at file load time. Key methods to
implement: `initialize` (name, url, arch_list, deps), `install_impl_internal`
(build logic), `expected_files` (validation). The `PackageManager` singleton
(`scripts/pkgmgr/package_manager.rb`) handles discovery, install/uninstall
orchestration, and status reporting.

Toolchain directory layout (same on both branches):
```
toolchain3/
  cache/                   # Downloaded tarballs (preserved across cleans)
  noarch/                  # Arch-independent packages (acpica, gtest src, lcov)
  <GCC_VER>/               # Per-GCC-version packages
    <arch>/                # Per-target-arch (i386, x86_64, riscv64)
    host_<host_arch>/      # Host-arch packages (mtools)
  syscc/                   # System-compiler packages
    host_<host_arch>/      # (cmake, gtest when built with system cc)
```

Packages are downloaded to `cache/` and extracted/built into the appropriate
arch directory. Downloaded tarballs survive `--clean` but not `--clean-all`.

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
