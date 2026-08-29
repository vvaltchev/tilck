
# Tilck Package Manager

## Contents

  * [Overview](#overview)
  * [Quick reference](#quick-reference)
  * [Architecture](#architecture)
    - [Toolchain layout](#toolchain-layout)
    - [Host tool tiers](#host-tool-tiers)
    - [Package lifecycle](#package-lifecycle)
  * [Dependency resolution](#dependency-resolution)
  * [Default packages and upgrades](#default-packages-and-upgrades)
  * [Atomic installs and signal safety](#atomic-installs-and-signal-safety)
  * [Resumable downloads](#resumable-downloads)
  * [Package reconfiguration](#package-reconfiguration)
  * [Test infrastructure](#test-infrastructure)
    - [Unit tests](#unit-tests)
    - [System tests](#system-tests)
    - [Code coverage](#code-coverage)
  * [Adding a new package](#adding-a-new-package)

## Overview

Tilck's package manager is implemented in Ruby (`scripts/pkgmgr/`) and manages
the cross-compilation toolchain: downloading, building, installing, uninstalling,
and upgrading packages. The entry point is `./scripts/build_toolchain`, which
bootstraps Ruby (>= 3.2, auto-downloaded if needed) and then exec's into
`scripts/pkgmgr/main.rb`.

The package manager handles three categories of packages:

  * **Target packages** (busybox, zlib, vim, tcc, ...): cross-compiled for the
    target architecture (i386, x86_64, riscv64) and linked into Tilck's image.

  * **Host packages** (mtools, gtest, cross-compilers): run on the build host.

  * **Noarch packages** (acpica, gnuefi_src, libmusl, lcov): source or headers
    used during the build, not compiled per-arch.

## Quick reference

```bash
# First-time setup (installs default packages for the current ARCH)
./scripts/build_toolchain

# List all packages and their install status
./scripts/build_toolchain -l

# Install specific packages (dependencies resolved automatically)
./scripts/build_toolchain -s vim tcc lua

# Uninstall a package
./scripts/build_toolchain -u vim

# Install cross-compiler for a specific arch
./scripts/build_toolchain -S riscv64

# Reconfigure a package interactively (make menuconfig)
./scripts/build_toolchain -C busybox
./scripts/build_toolchain -C busybox:1.36.1   # a specific version

# Install or remove a specific version (-s, -u and -C all take PKG:VER)
./scripts/build_toolchain -s host_ncurses:6.4
./scripts/build_toolchain -u host_ncurses:6.4

# Upgrade packages after a version bump in one of the version files
./scripts/build_toolchain --upgrade

# Check if upgrades are needed (used by CMake at configure time)
./scripts/build_toolchain --check-for-updates

# Run the package manager's own test suite
./scripts/build_toolchain -t

# Run with code coverage
./scripts/build_toolchain -t --coverage

# System tests: install all packages + build for all architectures
./scripts/build_toolchain -t --system-tests -a ALL

# Dry-run: see what would happen without executing
./scripts/build_toolchain -t -d --system-tests --all-build-types -a ALL
```

## Architecture

### Toolchain layout

```
toolchain4/
  cache/                            # Downloaded tarballs (preserved across cleans)
    partial/                        # Incomplete downloads (for resume)
  staging/                          # In-progress builds (atomic install)
  noarch/                           # Arch-independent packages
    acpica/<ver>/
    gnuefi/<ver>/
    libmusl/<ver>/
  gcc-<ver>/                        # Per-GCC-version cross-compiled packages
    i386/
      busybox/<ver>/
      zlib/<ver>/
    x86_64/
      ...
    riscv64/
      ...
  host/
    <os>-<host_arch>/               # e.g. linux-x86_64
      portable/                     # Tier 1: statically linked (cross-compilers)
        gcc-i386-musl/<ver>/
        gcc-x86_64-musl/<ver>/
      <distro>/                     # e.g. ubuntu-22.04
        mtools/<ver>/               # Tier 2: distro libc, any host CC
        ruby/<ver>/                 # Bootstrap Ruby (not a registered package)
        <host-cc>/                  # e.g. gcc-11.4.0
          gtest/<ver>/              # Tier 3: depends on host CC C++ ABI
```

### Host tool tiers

Host packages are placed in one of three tiers depending on their portability:

| Tier | `host_tier` | Path | When to use |
|------|-------------|------|-------------|
| 1 | `:portable` | `host/<os>-<arch>/portable/` | Statically linked, any distro (cross-compilers) |
| 2 | `:distro` | `host/<os>-<arch>/<distro>/` | Links distro libc, any host CC (mtools) |
| 3 | `:compiler` | `host/<os>-<arch>/<distro>/<host-cc>/` | C++ ABI dependent (gtest) |

### Package lifecycle

Each Ruby package is a class inheriting from `Package` (in `package.rb`). It
registers itself with `pkgmgr.register(MyPackage.new())` at file load time.

Key methods:
  * `initialize` — declares name, URL, arch_list, dep_list, host_tier, default, etc.
  * `install_impl_internal(dir)` — build logic (configure + make + make install)
  * `expected_files(ver)` — files/dirs that must exist after a successful build.
    Most packages ignore `ver`; it is there so a package whose install layout
    changed between versions can return a different list
  * `clean_build(dir)` — remove build artifacts (for recovery after interruption)
  * `config_impl` — interactive reconfiguration (optional, e.g. make menuconfig)
  * `build_env(ver)` — what this package offers to packages that depend on it:
    include dirs, lib dirs, pkg-config dirs, extra environment. The base class
    offers nothing; a package others link against overrides it
  * `dep_list_for(ver)` — the dependency list at a given version, for a package
    whose non-default versions need different dependency versions

### Version files

There are two, and they are unrelated:

| File | Keys | Holds |
|------|------|-------|
| `other/pkg_versions` | `VER_<PKG>` | versions that end up in Tilck (target side) |
| `other/host_pkg_versions` | `HOST_VER_<PKG>` | versions of build-host tools |

A package that exists on both sides — ncurses is both a Tilck library and a
host kconfig dependency — has one entry in each, and they may differ freely.
`pkgmgr.get_config_ver(name, host:)` takes the side explicitly rather than
guessing it from the name.

The keys carry different prefixes because all three readers (the Ruby package
manager, CMake, and the bash scripts that `source` them) hold both files in
one namespace.

At startup every registered package must resolve to a version in its file;
a missing entry is reported by name, not left as a silent nil.

### Build interfaces

A package that others build against publishes `build_env(ver)` and consumers
call `deps_build_env`, which merges what the whole dependency closure
publishes, nearest dependency first. No consumer names a dependency and the
base class knows no package: adding a library to `dep_list` is all it takes
for its flags to appear.

`BuildEnv` holds neutral data — include dirs, lib dirs, pkg-config dirs — and
renders it once at the end (`cflags`, `ldflags`, `env`, `kconfig_make_vars`).
That is what makes several providers combinable: two providers each handing
over a ready-made `HOSTCFLAGS=...` string would land as two assignments on the
same `make` command line, where GNU make keeps only the last.

## Dependency resolution

Packages declare dependencies via `dep_list`:

```ruby
dep_list: [Dep('ncurses', false)]                    # the default version
dep_list: [Dep('host_ncurses', true, ver: Ver('6.4'))]  # pinned exactly
```

Leaving `ver` out — the normal case — means "the default version", so the
version files stay readable as one coherent set instead of every edge
restating the version it would have got anyway. A package built at a
non-default version pins only the dependencies whose version has to differ,
not its whole closure.

Only host packages can be pinned; a pin on a target dependency is rejected,
because Tilck itself is built from exactly one version of each package.

Version selection (`version_solver.rb`) follows two rules:

  * an explicit pin beats an implicit default, and says so at info level —
    a default silently not being used is worth seeing;
  * two explicit pins that disagree are an error naming both paths, since
    nothing can satisfy both.

Within one resolution a package name therefore has exactly one version.

When installing a package, the dependency resolver:

  1. Collects the transitive closure of all dependencies (BFS)
  2. Filters out already-installed packages
  3. Topological-sorts the result (Kahn's algorithm, alphabetical tie-breaking)
  4. Installs in dependency order (deps before dependents)

Target packages also get an implicit dependency on the cross-compiler for the
current `ARCH` (injected by `build_dep_graph`).

The dependency graph is validated at startup for cycles and missing references.
If a cycle is detected, `build_toolchain` fails with a clear error before any
install attempt.

## Default packages and upgrades

When `build_toolchain` is run without arguments, it installs the **default
package set** for the current `ARCH` and `BOARD`:

  * **Always**: cross-compilers (x86 gets both i386 + x86_64), acpica,
    gnuefi_src, host_mtools, zlib, busybox
  * **x86 only**: gnuefi
  * **riscv64 only**: dtc, uboot (qemu-virt) or licheerv_nano_boot (licheerv-nano)

Each package's `default?` method determines if it's in the default set, gated
by `arch_supported?`, `host_supported?`, and `board_supported?`.

**Upgrades**: when a version is bumped in either version file, running
`build_toolchain --upgrade` (or just `build_toolchain` with no arguments)
installs the new version alongside the old one. The old version is NOT deleted.

Only installs that used the *default* version are upgraded. A version someone
asked for by name is deliberate and is left alone however old it is. The two
are indistinguishable from the directory tree alone — both are just
`<pkg>/<ver>/` — so each install records which it was in a hidden
`.install_origin` file. Installs predating that file read as default, which is
what they were: naming a version at install time is newer than they are.

CMake detects stale packages at configure time via `--check-for-updates` and
fails the build with a clear message telling the user to run `--upgrade`.
Both version files are `CMAKE_CONFIGURE_DEPENDS`, so `make` automatically
re-runs CMake when versions change.

## Atomic installs and signal safety

All package installs go through a **staging directory** (`toolchain4/staging/`).
The flow:

  1. Download to `cache/` (or use cached tarball)
  2. Extract to `staging/<pkg>/<ver>/` (or reuse from a previous interrupted run)
  3. Clean any stale build artifacts (if resuming from an interrupted build)
  4. Apply patches + configure + build in staging
  5. Verify `expected_files` in staging
  6. **Atomic `mv`** from staging to the final location

The final install directory is **never** in a partial state. Either it doesn't
exist (package not installed) or it was atomically moved after full verification.

On `SIGINT`, `SIGTERM`, `SIGHUP`, or `SIGQUIT` during the build step: a signal
handler cleans build artifacts from the staging dir (preserving extracted source),
prints a message, and exits. On the next run, the extracted source is reused and
only the build is repeated from scratch.

## Resumable downloads

HTTP downloads are resumable. If a download is interrupted, the partial file is
preserved in `cache/partial/` and the next attempt resumes from where it left off
using the HTTP `Range` header. If the server doesn't support resume (returns 200
instead of 206), the partial file is deleted and the download restarts. If the
range is invalid (416), the partial is also deleted.

## Package reconfiguration

Packages that support interactive configuration (busybox, u-boot) can be
reconfigured with `-C`:

```bash
./scripts/build_toolchain -C busybox
```

This runs `make menuconfig` in the installed package's source tree, then offers
to update the base config file (e.g. `other/busybox.config`) and rebuild.

## Test infrastructure

The package manager has a comprehensive test suite with 500+ tests. All tests
are run via `./scripts/build_toolchain -t`.

### Unit tests

Unit tests use minitest (Ruby stdlib) with a custom pretty reporter. They
exercise the package manager's logic using `FakePackage` instances and stubbed
externals (no real downloads, builds, or network access):

  * **test_version.rb** — version string parsing and comparison
  * **test_dep_resolver.rb** — dependency graph algorithms
  * **test_package.rb** — Package base class (default?, installed?, needs_upgrade?)
  * **test_package_manager.rb** — PackageManager (install, uninstall, resolve, upgrade)
  * **test_install_flow.rb** — install flow for target/host/noarch packages
  * **test_uninstall.rb** — uninstall with all parameter combinations
  * **test_show_status.rb** — package listing output
  * **test_main.rb** — CLI option parsing + integration tests via Main.main()
  * **test_cache.rb** — HTTP download + tar extraction (uses in-process test server)
  * **test_resume.rb** — resumable download scenarios
  * **test_git_clone.rb** — git clone with mock git layer
  * **test_progress.rb** — progress bar rendering and update throttling
  * **test_package_coverage.rb** — edge cases for 100% coverage on package.rb
  * **test_pkgmgr_coverage.rb** — edge cases for package_manager.rb
  * **test_build_env.rb** — BuildEnv: merging, de-duplication, rendering
  * **test_deps_build_env.rb** — what a package publishes and what a consumer
    collects, including which version each dependency resolves to
  * **test_version_solver.rb** — version selection: defaults, pins, pins that
    displace a default, and pins that conflict

Key testing patterns:
  * `with_fake_tc` — creates a temp toolchain directory, pins ARCH to i386
  * `with_stubbed_externals` — replaces Cache/run_command/with_cc with test doubles
  * `with_mock_git` — replaces git commands with configurable mock
  * `TestHTTPServer` — in-process HTTP server for download tests (supports Range, redirects, errors)

### System tests

System tests install real packages, build Tilck, and optionally run Tilck's
own test suites:

```bash
# Install all packages + build for all architectures
./scripts/build_toolchain -t --system-tests -a ALL

# Also run all 11 build generator configurations
./scripts/build_toolchain -t --system-tests --all-build-types -a ALL

# Also run Tilck's gtests + system tests (i386 and riscv64 only)
./scripts/build_toolchain -t --system-tests --run-also-tilck-tests

# Filter optional packages (faster iteration)
./scripts/build_toolchain -t --system-tests --test-packages-filter "vim|tcc"

# Dry-run: see the full execution plan without running anything
./scripts/build_toolchain -t -d --system-tests --all-build-types -a ALL
```

System tests wipe the toolchain (except cache and Ruby) before each architecture,
then install all default + optional packages from the cached archives.

### Code coverage

Coverage is collected using Ruby's built-in `Coverage` module (no external gems).
When `--coverage` is combined with `--system-tests`, coverage from subprocess
`build_toolchain` invocations is merged into the final report — so the actual
package build logic (configure, make, install) contributes to coverage alongside
the unit test logic.

```bash
./scripts/build_toolchain -t --coverage                  # Unit tests only
./scripts/build_toolchain -t --system-tests --coverage   # Unit + system tests
```

The HTML report is generated at `coverage_html/index.html`.

## Adding a new package

1. Create `scripts/pkgmgr/mypackage.rb`:

```ruby
class MyPackage < Package
  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'mypackage',
      url: 'https://example.com/releases',
      on_host: false,
      is_compiler: false,
      arch_list: ALL_ARCHS,       # or a subset
      dep_list: [],               # e.g. [Dep('zlib', false)]
      default: false,             # true if part of the default install
    )
  end

  def expected_files(ver = nil) = [
    ["mybin", false],             # file that must exist after build
  ]

  def clean_build(dir)
    system("make", "distclean", chdir: dir.to_s,
           out: "/dev/null", err: "/dev/null")
  end

  def install_impl_internal(install_dir)
    ok = run_command("configure.log", ["./configure", "--prefix=#{install_dir}"])
    return false if !ok
    ok = run_command("build.log", ["make", "-j#{BUILD_PAR}"])
    return ok
  end
end

pkgmgr.register(MyPackage.new())
```

2. Add `require_relative 'mypackage'` to `scripts/pkgmgr/main.rb`.

3. Add the version: `VER_MYPACKAGE=1.0.0` in `other/pkg_versions` for a
   target package, or `HOST_VER_MYPACKAGE=1.0.0` in
   `other/host_pkg_versions` for a host one.

4. Run: `./scripts/build_toolchain -s mypackage`


[building]: building.md
