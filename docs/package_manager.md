
# Tilck Package Manager

## Contents

  * [Overview](#overview)
  * [Quick reference](#quick-reference)
  * [Architecture](#architecture)
    - [Toolchain layout](#toolchain-layout)
    - [Host tool tiers](#host-tool-tiers)
    - [Package lifecycle](#package-lifecycle)
  * [Dependency resolution](#dependency-resolution)
    - [Where a version comes from](#where-a-version-comes-from)
    - [A package's own version can decide its dependencies'](#a-packages-own-version-can-decide-its-dependencies)
  * [System dependencies](#system-dependencies)
  * [Default packages and upgrades](#default-packages-and-upgrades)
  * [Build identity](#build-identity)
  * [How the build system finds the toolchain](#how-the-build-system-finds-the-toolchain)
  * [Atomic installs and signal safety](#atomic-installs-and-signal-safety)
  * [Resumable downloads](#resumable-downloads)
  * [Package reconfiguration](#package-reconfiguration)
  * [Test infrastructure](#test-infrastructure)
    - [Unit tests](#unit-tests)
    - [System tests](#system-tests)
    - [Correctness guarantees](#correctness-guarantees)
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

# Check if anything needs upgrading or rebuilding (CMake runs this)
./scripts/build_toolchain --check-for-updates

# Print where installed packages live, as KEY=value (CMake runs this too)
./scripts/build_toolchain --print-layout

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

Every installation lives at three coordinates, always in the same
order, each with a fixed meaning:

```
toolchain5/<machine>/<env>/<stack>/{ sysroot/, pkgs/<pkg>/<ver>/ }
```

| coordinate | answers | example values |
|------------|---------|----------------|
| `<machine>` | where does it RUN? | `linux-x86_64`, `tilck-i386`, `noarch` |
| `<env>` | which environment does it belong to? | `any`, `ubuntu-22.04`, `pc`, `qemu-virt` |
| `<stack>` | which build environment made it? | `any`, `gcc-14.4.0`, `gcc-13.3.0` |

`<env>` is *what the machine must already provide*; `any` means the
artifact is self-contained. For a Tilck target it names the board
instead — the same question asked of a system we configure rather than
one we find. `pc` is the board for i386 and x86_64.

`<stack>` is deliberately not called "the compiler". It is the identity
of a build environment, which today is always a compiler but must be
free to become `gcc-14.4.0-lto` without a schema change. `any` means no
particular build environment matters: a static binary, or a blob.

A package name can appear only under `pkgs/`, so it can never be
mistaken for structure. `sysroot/` exists exactly when we built the
environment — when `<env>` is `any` and `<stack>` is ours.

```
toolchain5/
  cache/                                # Tarballs (kept across cleans)
    partial/                            # Incomplete downloads (for resume)
  staging/                              # In-progress builds (atomic install)

  linux-x86_64/any/any/                 # Static: needs nothing from the host
    pkgs/ gcc-i386-musl/<ver>/          #   the musl cross-compilers
          gcc-x86_64-musl/<ver>/
          gcc-riscv64-musl/<ver>/
          sophgo_tools/<ver>/           #   a prebuilt board tool

  linux-x86_64/any/gcc-14.4.0/          # Our stack: our glibc and loader
    sysroot/                            #   a view of the stack, not an install
    pkgs/ glibc/<ver>/  glib2/<ver>/  gtk3/<ver>/  qemu/<ver>/ ...

  linux-x86_64/ubuntu-22.04/any/        # Needs this distro's libc
    pkgs/ binutils/<ver>/  mtools/<ver>/  ninja/<ver>/  meson/<ver>/
          mconf/<ver>/  ncurses/<ver>/
          gcc/<ver>/                    #   our host gcc: system cc built it
          ruby/<ver>/                   #   bootstrap Ruby (not a package)

  linux-x86_64/ubuntu-22.04/gcc-11.4.0/ # + needs that host C++ ABI
    pkgs/ gtest/<ver>/

  noarch/any/any/
    pkgs/ acpica/<ver>/  gnuefi/<ver>/  lcov/<ver>/  libmusl/<ver>/

  tilck-i386/pc/gcc-13.3.0/             # Target packages, per arch AND board
    pkgs/ busybox/<ver>/  zlib/<ver>/  vim/<ver>/ ...

  tilck-riscv64/qemu-virt/gcc-13.3.0/
    pkgs/ busybox/<ver>/  dtc/<ver>/  uboot/<ver>/ ...

  tilck-riscv64/licheerv-nano/gcc-13.3.0/
    pkgs/ busybox/<ver>/  licheerv_nano_boot/<ver>/ ...
```

Two boards of one architecture are two separate trees. They were not,
until an install for one silently answered for the other: `-s ALL` with
`BOARD=licheerv-nano` planned nineteen packages, installed two, and
left the board without a C library — every package reporting as already
installed on the strength of the qemu-virt build in a different
directory.

**New axes become values, never levels.** A fourth coordinate would put
the schema back where toolchain4 ended up, with a directory name whose
meaning depends on its depth. Anything else that needs distinguishing
becomes a new *value* of one of the three — a new board, a new stack
name — or, if it truly cannot, `<stack>` collapses to an opaque id with
a manifest beside it.

### Host tool tiers

A host package's directory answers exactly one question: **where can this
run?** Another machine sharing the toolchain reads the answer off the path
and knows whether to consume the package or rebuild it.

| `host_tier` | `<env>` / `<stack>` | Runs on |
|-------------|---------------------|---------|
| `:portable` | `any` / `any` | any host of this OS + arch — statically linked (cross-compilers) |
| `:stack` | `any` / `gcc-<ver>` | the same, carrying our own glibc and loader (glibc, glib2, GTK, QEMU) |
| `:distro` | `<distro>` / `any` | this distro only — links its libc (mtools, our gcc and binutils) |
| `:compiler` | `<distro>` / `<host-cc>` | + this host C++ ABI (gtest) |

`:stack` and `:portable` share an `<env>` of `any`, because both run
anywhere: our compiler is a *variant key*, not a restriction on where the
result runs. It is in the path for the same reason `:compiler` carries the
host compiler — a GCC bump can change the C++ ABI, so the whole set is
rebuilt beside the old one rather than in place, and several stacks
coexist as siblings without a new level.

The tier is **declared** per package and never inferred from a build's
outcome: `--prefix` and RPATH are baked in at configure time, so the
directory has to be known before anything is built. The portability audit
(`scripts/pkgmgr/portability.rb`) **enforces** the declaration — a package
declared portable whose binaries reference anything outside the toolchain
fails its install. A `:distro` package makes no such promise and is not
asked to keep it.

A composed sysroot sits beside `pkgs/`, not inside it: a sysroot is a
*view* over installed packages rather than an installation, and putting
packages one level down means no scanner has to be taught to skip it.

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

### Where a version comes from

Exactly two places, and the order matters:

  * **a pin**, if something in this resolution named one — a `ver:` on a
    dependency edge, or `PKG:VER` on the command line;
  * **the default** from the version file, otherwise.

So the default in `other/host_pkg_versions` is what a package gets when
*nobody said otherwise*: when the user types `-s host_glib2` with no
version, and when a dependency edge points at it without one. It is not a
global that overrides anything; it is the answer to a question nobody
asked.

### A package's own version can decide its dependencies'

`dep_list` is the graph — who depends on whom — and it is version-less by
design, because the graph does not change with the version. What may change
is which versions those edges carry, and a package says so by overriding
`dep_list_for(ver)`:

```ruby
# host_qemu: each QEMU is built by a compiler from its own time.
def dep_list_for(ver = nil)
  gcc = GCC_FOR[(ver || default_ver).series]
  return dep_list if gcc.nil?

  # Replace rather than append: the same package named twice, once bare
  # and once pinned, leaves the winner to the solver's walk order.
  base = dep_list.reject { |d| d.name == "host_gcc" }
  return base + [Dep('host_gcc', true, ver: gcc)]
end
```

Two packages do this today, for the same reason — a build is a pairing, not
a package:

| Package | Carries | Because |
|---------|---------|---------|
| `host_gcc` | gmp, mpfr, mpc, isl | GCC 11 wants gmp 6.1.0 where GCC 16 wants 6.3.0; its own sources were tested against those |
| `host_qemu` | the compiler | building a 2021 QEMU with a 2026 GCC tests neither of them |

The effect is that asking for one package can bring a whole world with it.
With `HOST_VER_GCC` naming 14.4.0:

```
$ ./scripts/build_toolchain -s host_qemu:7.2.0
INFO: host_gcc:  using 12.5.0, not the default 14.4.0
      (pinned via host_qemu -> host_gcc)
INFO: host_gmp:  using 6.1.0, not the default 6.2.1
      (pinned via host_qemu -> host_gcc -> host_gmp)
INFO: Building into the gcc-12.5.0 stack
```

The pins compose: QEMU names its compiler, that compiler names its maths
libraries, and the default reaches none of them. `-s host_glib2` on its own
still gets the default, because nothing pinned it.

That last line is the other half. A `:stack` package lives under the
compiler that built it, so the host_gcc version a request resolves to also
decides which stack the install goes into — `-s host_qemu:7.2.0` builds
into `gcc-12.5.0` however `HOST_VER_GCC` is set, and `-H` (see `--help`)
moves the default for a run that has no pin to follow.

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

## System dependencies

Some packages need things the package manager does **not** build: a Rust
toolchain, a `-dev` library, a code generator. A package declares those by
overriding `system_deps`, and the install path checks the union of the
declarations across the whole resolved closure **before building anything** —
so a missing toolchain stops the run in the first second, instead of forty
minutes in when a `configure` script finally goes looking for it.

The generic half lives in `scripts/pkgmgr/system_pkgs.rb`: one `Backend` per
host package manager (apt, dnf, pacman, FreeBSD `pkg`, Homebrew), each knowing
how to query what is installed and how to install more. It has no dependency on
`Package` or `PackageManager`, so anything else that needs to check for a host
tool can use it as-is. `scripts/pkgmgr/system_deps.rb` builds the declarations
and the check flow on top.

This is the Ruby counterpart of `scripts/bash_includes/install_pkgs`, which
still does the bootstrap — it runs before Ruby exists, so it cannot be replaced
outright. But that list is installed **unconditionally on every machine**,
whether or not a given package is ever built, which is why it must stay small.
Anything needed by only some builds belongs here instead, and over time
requirements can move out of `install_pkgs` and into the packages that actually
want them.

### Two shapes of requirement

A **package** — `libssl-dev` — is named per backend and only the host package
manager can answer for it:

```ruby
SysDep.new(key: :ssl, what: "OpenSSL headers",
           pkgs: { apt: "libssl-dev", dnf: "openssl-devel" })
```

A **command** — `rustc` — is checked by *running* it, and may carry a minimum
version:

```ruby
SysDep.new(key: :rustc, what: "the Rust compiler", command: "rustc",
           min_ver: Ver("1.85"), installer: SystemDeps::RUSTUP)
```

The distinction is not cosmetic. A command can arrive from somewhere the
package manager has never heard of — rustup, Homebrew, a hand-built tree on
`PATH`. On the machine this was written on, `dpkg-query` reports `rustc` as not
installed while a perfectly good rustup toolchain sits in `~/.cargo/bin`;
asking the package manager would send you installing a *worse* rustc than the
one already there. So commands are looked for on `PATH` plus the prefixes those
installers use, and their `--version` is parsed and compared — the same thing
`version_check.rb` does to the Ruby it is running under.

A version floor requires `command`: it is not portably expressible across five
package managers, and a constraint that cannot be checked would read as
satisfied everywhere.

### Outcomes

| State | Meaning |
|---|---|
| `ok` | present, and new enough if a floor was declared |
| `missing` | not found at all |
| `too_old` | found, but below `min_ver` |
| `broken` | on disk, but would not run or report a version |
| `unknown` | no backend for this distro, or no package name for it there |

`unknown` is reported rather than guessed at: on an unsupported distro,
claiming a dependency is satisfied would be a lie, and claiming it is missing
would send people installing something already present under another name. It
warns and lets the build proceed.

`too_old` is deliberately **not** routed to the host package manager —
reinstalling what is already there fixes nothing.

### Installing

Missing packages are batched into a single command, shown, and installed after
a prompt. After any install, every requirement is checked **again from
scratch**: an install command exiting 0 is not evidence the requirement is met
— a package manager will happily install a `rustc` that is still too old.

Some dependencies are better served by their own installer than by the distro.
Rust is the case in point: Ubuntu 22.04 ships 1.66 and cannot be updated to
what modern crates need, so `SystemDeps::RUSTUP` offers rustup instead.

Both routes follow the **same** policy, decided by two predicates in
`system_pkgs.rb`: `interactive?` is `STDIN.tty? && STDOUT.tty?` (both, because
a prompt written to a redirected stdout is invisible), and `in_ci?` is a
non-empty `RUNNING_IN_CI` or `CI`.

| | host package manager | rustup |
|---|---|---|
| a terminal | prompt, default **yes** | prompt, default **yes** |
| no tty, `CI=1` | install with `-y` | install |
| no tty, no `CI` | refuse, print the command | refuse, say what to install |

Setting `CI=1` by hand is therefore the way to get an unattended auto-yes run.
`-y` is passed to the package manager only in that case; the command *shown* to
a human never carries it, because somebody typing it wants to see what their
package manager proposes first.

When rustup runs interactively it also asks whether to add `~/.cargo/bin` to
`PATH` permanently. This build does not need that — the directory is prepended
to the pkgmgr process's own `PATH` either way — but somebody installing Rust
usually wants it for other things too, so it is asked rather than decided. An
unattended run passes `--no-modify-path`: nobody was there to consent to a
shell profile being edited.

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

## Build identity

A version number is not enough to say an install is current. Add a patch
to a package, change a configure flag, edit the build steps — the version
is the same and what is installed no longer matches the sources it claims
to come from.

So each install records what it was built FROM, in a hidden
`.build_inputs` beside what was built: a digest of the *recipe* (the
declared flags, the build steps, and the source of the methods the
package itself defines, comments excluded) plus a digest of every patch
file that applies to it.

`--check-for-updates` compares each record against the sources present
now, and reports three distinguishable states:

| state | meaning | remedy |
|-------|---------|--------|
| `ok` | built from the sources we have | — |
| `changed` | built from something else | `-s <pkg> -f` |
| `unknown` | no record at all | `-s <pkg> -f` |

`unknown` is reported rather than assumed benign. Every install is made
by this mechanism, so a missing record means something went wrong while
writing it — which is exactly the case that stays invisible if a missing
record counts as fine.

**A recipe is only a recipe at some coordinates.** A build step may name
the archiver as `#{default_arch.gcc_tc}-linux-ar`, so the same version
installed for two architectures was genuinely built from two different
recipes. Each record is therefore written, and checked, against the
recipe as it reads at *that install's* coordinates. A digest quoted
without the coordinates it was computed for means nothing.

The digest must also be **reproducible**: it may depend on the sources
and on nothing else. A flag naming an absolute path built from the
current working directory once made the same tree report as stale from a
build directory and fresh from the repository root — an instrument that
invents work is one people learn to ignore.

## How the build system finds the toolchain

CMake does not construct install paths. It asks:

```
$ ./scripts/build_toolchain -q --print-layout
ARCH=i386
BOARD=pc
HOST_DISTRO=ubuntu-22.04
HOST_CC=gcc-11.4.0
TCROOT=/home/user/tilck/toolchain5
PKGS_HOST_PORTABLE=.../linux-x86_64/any/any/pkgs
PKGS_HOST_DISTRO=.../linux-x86_64/ubuntu-22.04/any/pkgs
PKGS_HOST_CC=.../linux-x86_64/ubuntu-22.04/gcc-11.4.0/pkgs
PKGS_NOARCH=.../noarch/any/any/pkgs
PKGS_TARGET=.../tilck-i386/pc/gcc-13.3.0/pkgs
PKGS_TARGET_<arch>=...              # one per arch, at its own default board
```

The alternative is the schema written down twice, and the copies drift:
CMake went on describing toolchain4's layout after every install had
moved, so a build looked for directories nothing creates. `Coords`
(`scripts/pkgmgr/coords.rb`) stays the only thing that knows what a path
looks like.

The answer also carries `ARCH`, `BOARD`, `HOST_DISTRO` and `HOST_CC`,
which CMake derives independently. They are compared, and a
disagreement stops configure rather than surfacing much later as a
missing file in a subtree nobody thought to look at.

Which directory the toolchain itself lives in is named once, in
`other/toolchain_conf`, and read by the bash bootstrap, the package
manager, CMake, the root `Makefile` and the CI test wrapper. A
generation bump moves every path, so a consumer left behind looks for a
directory that is not built any more.

## Atomic installs and signal safety

All package installs go through a **staging directory** (`toolchain5/staging/`).
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

The package manager has a comprehensive test suite with 1000+ tests. All tests
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

### Correctness guarantees

The package manager's *logic* -- placement, identity, scoping,
dependency and version resolution, staleness, listing -- is held to a
stronger standard than "well tested". Four instruments, each answering
a question the others cannot:

**The lint** (`tests/test_lint_ambient.rb`) parses every source file
with Prism and fails the suite if anything reads `ARCH`, `BOARD` or
the current stack outside their owners (`pkgmgr.target_arch`,
`board_for`, `current_host_stack`), compares an installation by part
of a coordinate (`.arch ==`, `.compiler ==`) outside `InstallSelector`,
or writes a scope variable outside its `with_*` method. Every logic
bug the package manager has had was one of those three, and this is
what makes the class unwritable rather than merely caught. The
allowlist lives in the test, each entry with a reason, and an entry
that no longer names a real method fails the test.

**The model** (`tests/model/model.rb`) is the contract as a program:
a world is a set of installations `(name, version, coordinates,
record, origin)`, and every command line is a pure function from
(registry, world, invocation) to (exit code, world', output). No I/O,
no packages, small enough to audit. It is validated before it judges
anything: every historical bug is a case in `test_model.rb` with the
answer written by hand, and the model's own laws (`select` is total,
dry-run changes nothing, determinism) hold.

**The laws** (`tests/laws.rb`) run around every command line the
suite drives, inside `TestHelper#run_cli`: the world after equals what
the model computes (L1); `-d` changed nothing (L2); every installation
sits where its package says, judged at its own coordinates (L3);
everything installed carries a record that reads ok (L4). A test about
`-l`'s output is thereby also a test that `-l` changed nothing. The
runner prints how many lines were judged and how many fell outside
the model's grammar.

**The exhaustive lane** (`tests/exhaustive/`) is the theorem. For
fifteen registry shapes -- one feature each -- it enumerates every
world of at most two installations, every invocation context, and
every command line in the grammar, and hands each case to the laws.
About sixty-six thousand cases; `-t --exhaustive` runs them all,
one process per shape, in every toolchain workflow, and every `-t`
runs a fixed-seed sample of a thousand. It self-tests first (a
snapshot equals a second snapshot, a world reads back as built, a
planted disagreement is seen) and refuses to run otherwise. A failure
prints its id, the world, the argv and both worlds; `--case ID`
replays it.

**Mutation** (`scripts/dev/claude/pmmutate`) is the certificate that
the above is enough. Each of ~450 sites in the logic core is
rewritten one way it could be wrong -- a comparison flipped, a
conjunct dropped, a guard deleted, `nil` for `"ALL"`, a scope not
opened or not restored, the ways this tree has actually been wrong --
and the suite must fail. A survivor is a test that does not exist,
named to the line. The score to defend is zero survivors in scope.

What this proves, mechanically, on every commit: for the catalogue of
shapes and worlds of two, the implementation's effect on the tree and
its answers equal the model's; the implementation reads its inputs
only through their owners; and every line of the logic core is
defended by a test that fails if it is wrong. What it does not prove:
worlds of three or more (the bound rises when a bug appears there --
none has), shapes outside the catalogue (add one when a package with
a new feature appears), and anything about the real recipes or the
network.

When a logic bug is found, the fix touches all four: the model says
the right answer (or is corrected first), a shape or a line is added
so the lane fails before the fix and passes after, and the mutant
that reproduces the bug is expressible and killed. See CLAUDE.md.

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
