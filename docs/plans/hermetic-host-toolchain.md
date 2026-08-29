# Hermetic host toolchain and QEMU build

Status: **design, not yet implemented**. This document is the plan of
record; it is expected to gain a gap list and lose items from it as the
work lands.

## Goal

Build QEMU, with its full GTK UI and every target Tilck runs on, from
source, linking against **nothing from the host system** — glibc
included. The compiler itself is part of the picture: we build GCC and
glibc too.

This is buildroot's problem, solved our own way, inside the existing
package manager rather than beside it.

### Non-goals

  * Replacing the existing host packages. `host_ncurses`, `host_gtest`,
    `host_mtools` and `host_mconf` keep building exactly as they do
    today. Hermeticity is opt-in per package.
  * Being in the default install set. The whole stack is second-tier
    and gated behind an explicit flag or environment variable: a
    30-60 minute toolchain build must never be something a contributor
    trips over.
  * Relocatability of the toolchain tree to an arbitrary path. It lives
    at a known absolute path per checkout.
  * Rust. QEMU 9+ can optionally use it; we deliberately do not.

## Hermetic-dynamic, not static

Every binary we produce is dynamically linked, but exclusively against
our own libraries:

  * the ELF interpreter is **our** loader;
  * `DT_RPATH` points into our lib directories;
  * `/usr/include` and `/usr/lib` never appear on any search path.

Both of the first two are required, and it is worth being precise about
why the second is not optional. Our `ld.so` has the sysroot compiled in
as its default search path, so a binary carrying **no** RPATH resolves
correctly whenever nothing competes — and silently loads system
libraries the moment something does:

```
$ LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu our-loader --list prog
        libstdc++.so.6 => /usr/lib/x86_64-linux-gnu/libstdc++.so.6
        libc.so.6      => /usr/lib/x86_64-linux-gnu/libc.so.6
```

Hermeticity has to be a property of the binary, not of the environment
it happens to run in.

`DT_RPATH` specifically, not `DT_RUNPATH`: RPATH is searched *before*
`LD_LIBRARY_PATH` and RUNPATH *after* it, so only the former is immune.
That means `--disable-new-dtags`, passed explicitly rather than relying
on binutils' build-time default.

The audit therefore asks its question with a hostile `LD_LIBRARY_PATH`.
A clean-environment check would pass a binary that is only accidentally
hermetic, which is exactly what it did before this was noticed.

Fully static was considered and rejected: GTK loads pixbuf loaders,
input methods and themes as shared modules at runtime, so a fully
static GTK-enabled QEMU is not reliably reachable.

## The bootstrap

GCC needs a libc to link against and glibc needs a compiler, which is
why cross toolchains bootstrap in five stages (binutils, stage-1 GCC,
kernel headers, glibc, stage-2 GCC).

**We do not need that**, because we build for the same triple as the
host (`x86_64-pc-linux-gnu`). The system GCC is already a valid
compiler for the target, so it can build glibc directly and the
cycle never forms:

```
1. binutils          built with the system compiler
2. linux headers     make headers_install — no compiler involved
3. glibc             built with the SYSTEM gcc, installed into our sysroot
4. gcc               ONE build, --with-sysroot=<ours>, C and C++,
                     linked against the glibc from step 3
```

The system compiler is used as a build-time input and nothing more. It
leaves no trace in the output: a GCC built by Ubuntu's 11.4 and one
built by Fedora's 14 generate identical code from identical source, so
using it does not weaken hermeticity.

### Rejected: building GCC with the pre-built musl toolchain

Superficially attractive — the musl cross-compilers are already in the
tree and pinned, so the bootstrap input would be identical on every
machine. It was rejected because it turns a native build into a
**Canadian cross**: GCC's configure would see build =
`x86_64-pc-linux-gnu`, host = musl-targeting, target =
`x86_64-pc-linux-gnu`. That is the most fragile configuration GCC has,
and declaring `--host` otherwise puts configure's own probes at odds
with the declared host.

The reproducibility it would buy is largely illusory, per the point
above: what builds GCC does not determine what GCC produces.

### Deferred: recursive GCC bootstrap

If a GCC we want ever requires a newer bootstrap compiler than the
oldest supported build host provides, the answer is a second,
older `host_gcc` built with the system compiler and used to build the
real one.

Not needed today: GCC 14 requires an ISO C++14 compiler (GCC 6.1+),
and the oldest build host we care about — Ubuntu 22.04 — ships GCC
11.4. Every CI container ships 12 or newer. Because `host_gcc` is
versioned and pinnable, adding this later needs no rework.

## Triple: same, with a sysroot

We build for `x86_64-pc-linux-gnu`, the host's own triple, and point
the toolchain at our sysroot via `--with-sysroot`. This is what a
distro does inside a chroot.

The alternative — a distinct vendor triple such as
`x86_64-tilck-linux-gnu`, which is what buildroot does — was rejected.
It makes autotools treat every package as a cross-build, which means
configure scripts cannot *run* their test programs and every wrong
autodetection has to be corrected by hand through cached configure
answers. Buildroot carries a large accumulation of exactly those.

With the same triple, test programs run correctly (same architecture
and ABI, our loader present), so configure results are accurate.

The risk this accepts is system-path leakage: a configure script that
hardcodes `-I/usr/include` would silently un-hermeticise the build.
That risk is *detectable*, which is why the audit below is not
optional.

## Layout: a fourth host tier

The three existing host tiers all exist because the system libc and
compiler vary between machines. Hermeticity makes both irrelevant, so
neither `:distro` nor `:compiler` describes these packages.

```
toolchain4/host/linux-x86_64/
  portable/                       tier 1  static, any distro   (unchanged)
  <distro>/                       tier 2  distro libc          (unchanged)
  <distro>/<host-cc>/             tier 3  C++ ABI dependent    (unchanged)
  hermetic/<host-gcc-ver>/        tier 4  our libc             (NEW)
     toolchain/                   binutils + gcc + glibc: the compiler
     sysroot/                     composed view (see below)
     <pkg>/<ver>/                 each package, pkgmgr's normal layout
```

Keyed by our GCC version, so bumping the compiler builds the new stack
beside the old one instead of in place — the same "install alongside,
never replace" property the rest of the package manager has.

## Package decomposition

An earlier draft of this document had GCC and glibc as one package, on
the grounds that they depend on each other and Tilck resolves a
dependency cycle by merging the cyclic packages.

**That was wrong**, and the same-triple decision is why. A cross
toolchain has a cycle because it has no compiler for its target until
it builds one. Building for the host's own triple means the system
compiler is already valid for the target, so glibc is built by it
directly and nothing in the chain needs our GCC:

```
binutils (system cc) -> linux headers (no cc) -> glibc (system cc)
                     -> gcc (system cc, against that glibc)
```

Linear, so the packages stay separate.

The tier each lands in describes what that package's **own binaries**
depend on:

| Package | Tier | Contains |
|---|---|---|
| `host_binutils` | `:distro` | as/ld/ar — build tools, system-linked |
| `host_linux_headers` | `:hermetic` | sysroot headers |
| `host_glibc` | `:hermetic` | sysroot libc |
| `host_gcc` | `:distro` | the compiler, system-linked |

binutils and GCC are built by the system compiler and link the system
libc, so they are ordinary `:distro` packages. That is not a
compromise: they are build tools running on the host, and what they
link against never reaches what they produce. GCC's *target* runtime
(libgcc, libstdc++) is a different matter — it is built against our
glibc and belongs to the sysroot, which the symlink farm composes from
GCC's install directory.

One binutils serves every hermetic stack. A GCC needing a particular
one pins it with `Dep('host_binutils', true, ver: ...)`.

### Supported GCC versions

Majors 11 through 16, at the latest point release of each, per
ftp.gnu.org: 11.5.0, 12.5.0, 13.4.0, 14.4.0, 15.3.0, 16.2.0. The
default is 14.4.0. Each installs beside the others and gets its own
`hermetic/gcc-<ver>/` stack.

`host_gcc`'s own version is the GCC version. The versions of the pieces
it builds are separate entries it reads, so they stay visible and
pinnable without pretending to be independently installable:

```
# other/host_pkg_versions
HOST_VER_BINUTILS=<ver>
HOST_VER_GCC=<ver>
HOST_VER_GLIBC=<ver>
HOST_VER_LINUX_HEADERS=<ver>
```

## glibc and the minimum kernel

Because we ship our own loader, the host's glibc is irrelevant to us.
The only remaining host constraint is the **kernel**, and that floor is
set at build time by `--enable-kernel=X.Y` — independently of the glibc
version and of the kernel headers compiled against.

Decision: current-stable glibc, built with `--enable-kernel=4.19` (an
LTS from 2018). Ubuntu 22.04, the oldest build host we support, runs
5.15, so this clears it by a wide margin and covers every CI container.
Kernel headers may be recent; they do not raise the runtime floor.

## Composed sysroot

The package manager installs each package into its own versioned
directory, and `deps_build_env` hands consumers `-I` / `-L` /
`PKG_CONFIG_PATH` per dependency. That is preserved: versions stay
independently installable and removable.

On top of it, a **composed sysroot** — a symlink farm materialised from
the resolved version set:

```
sysroot/usr/include/glib-2.0    -> ../../../host_glib2/<ver>/install/include/glib-2.0
sysroot/usr/lib/libglib-2.0.so  -> ../../../host_glib2/<ver>/install/lib/libglib-2.0.so
```

A 95-library GTK stack will contain packages that assume a single
prefix, and a per-package `-L` list produces an unusable RPATH. The
sysroot solves both.

It composes with the version solver: the solver decides *which*
versions, and the sysroot is the rendering of that decision. It is
recomposed whenever the resolution changes.

## The hermeticity audit

"Hermetic" rots silently. One configure script slipping `-I/usr/include`
through is enough, and nothing fails at the time — it fails months
later, on a different machine.

So every hermetic package install is followed by an audit that walks
the produced binaries and shared libraries and fails if:

  * `ldd` resolves anything outside `toolchain4/`;
  * the ELF interpreter is not our loader;
  * any `RPATH`/`RUNPATH` entry points outside `toolchain4/`.

This runs as part of the install, the way `expected_files` does, and is
a first-class deliverable rather than a debugging aid.

## Build order

  1. `host_binutils`
  2. `host_gcc` (kernel headers, glibc, gcc)
  3. the hermeticity audit, proven against the toolchain itself
  4. one trivial leaf library, end to end, to establish the pattern
  5. the QEMU dependency closure, bottom up
  6. QEMU itself, newest major first

Newest QEMU major first is deliberate: its dependencies sit at versions
their upstreams actively test against current toolchains, so the
failures encountered are the real ones (musl/glibc assumptions, linking)
rather than archaeology against a modern compiler. Older majors are
backfilled afterwards using per-version dependency pins, which the
package manager already supports:

```ruby
def dep_list_for(ver)
   return [Dep('host_glib2', true, ver: Ver('<older>'))] if ver < Ver('7.0')
   dep_list
end
```

## Gap list

Tracked to zero before this is considered done.

  * [x] `host_binutils` package
  * [x] `host_linux_headers` package
  * [x] `host_glibc` package
  * [x] `host_gcc` package (majors 11-16 declared; 14.4.0 built)
  * [x] tier 4 (`host_tier: :hermetic`) in the package manager
  * [x] composed sysroot + recomposition on resolution change
  * [x] hermeticity audit
  * [x] opt-in gating (`TILCK_HERMETIC=1`)
  * [ ] measured QEMU dependency closure, enumerated in this document
  * [ ] the closure, package by package
  * [ ] QEMU, newest major
  * [ ] full GTK UI verified working
  * [ ] every Tilck target present in the built QEMU
  * [ ] older QEMU majors via per-version pins
