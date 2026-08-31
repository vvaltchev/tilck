# toolchain5: layout and build identity

## Why toolchain4's layout stopped working

`gcc-*` came to mean four unrelated things, decided only by where it
appears:

```
toolchain4/gcc-13.3.0/i386/busybox/1.36.1/         a cross-compiler VERSION
toolchain4/host/…/portable/gcc-i386-musl/13.3.0/   a PACKAGE (the compiler)
toolchain4/host/…/portable/gcc-14.4.0/glib2/…      a BUILD-COMPILER axis
toolchain4/host/…/ubuntu-22.04/gcc-11.4.0/gtest/…  the host C++ ABI slot
toolchain4/host/…/ubuntu-22.04/gcc/14.4.0/         a PACKAGE (our host_gcc)
```

The third is the mistake. `host/` meant *built with the host compiler*,
and putting packages built with **our** compiler under it broke that at
a level where a directory name is indistinguishable from a package
name. `PackageManager#compiler_slot?` exists only to guess which is
which — a predicate that disambiguates two meanings of one name is the
symptom, not the fix.

## The schema

```
toolchain5/<machine>/<env>/<stack>/{ sysroot/, pkgs/<pkg>/<ver>/ }
```

Three coordinates, always, each with a fixed meaning. A package name
can appear only under `pkgs/`, so it can never be mistaken for
structure.

| coordinate | answers | example values |
|---|---|---|
| `<machine>` | where does it RUN? | `linux-x86_64`, `tilck-i386`, `noarch` |
| `<env>` | which environment does it belong to? | `any`, `ubuntu-22.04`, `pc`, `qemu-virt` |
| `<stack>` | which build environment made it? | `any`, `gcc-14.4.0`, `gcc-13.3.0` |

`<env>` is *what the machine must already provide*: `any` means the
artifact is self-contained. For a Tilck target it names the board or
system variant instead — the same question ("which environment does
this belong to"), asked of a system we configure rather than one we
find. `pc` is the board for i386 and x86_64.

`<stack>` is deliberately NOT called "the compiler". It is the
identity of a build environment, which today is always a compiler but
must be free to become `gcc-13.3.0-musl` or `gcc-14.4.0-lto` without a
schema change. `any` means no particular build environment matters —
a statically linked binary, or a prebuilt blob.

`sysroot/` exists exactly when we built the environment, i.e. when
`<env>` is `any` and `<stack>` is ours.

## Today's toolchain4, mapped

81 installed packages, 7 stacks, computed from the tree rather than
imagined:

```
toolchain5/
├── cache/                                   downloaded tarballs
├── staging/                                 build trees, always
│
├── linux-x86_64/any/any/                    [4]  static, stack-agnostic
│   └── pkgs/ gcc-i386-musl/  gcc-x86_64-musl/
│             gcc-riscv64-musl/  gcc-aarch64-musl/
│
├── linux-x86_64/any/gcc-14.4.0/             [44] our stack
│   ├── sysroot/
│   └── pkgs/ glibc/2.41/  glib2/2.88.3/  gtk3/3.24.52/  qemu/6.2.0/
│             librsvg/  libglycin/  glycin-loaders/  + 37 more
│
├── linux-x86_64/ubuntu-22.04/any/           [15] needs this distro
│   └── pkgs/ ruby/3.4.7/  binutils/  mtools/  ninja/  meson/  mconf/
│             ncurses/  gcc/11.5.0/ … gcc/16.2.0/
│
├── linux-x86_64/ubuntu-22.04/gcc-11.4.0/    [1]  + needs that C++ ABI
│   └── pkgs/ gtest/1.17.0/
│
├── noarch/any/any/                          [5]
│   └── pkgs/ acpica/  gnuefi/  lcov/  libmusl/  tfblib/
│
├── tilck-riscv64/qemu-virt/gcc-13.3.0/      [9]
│   └── pkgs/ busybox/  dtc/  lua/  micropython/  ncurses/  tcc/
│             treecmd/  uboot/  zlib/
│
└── tilck-x86_64/pc/gcc-13.3.0/              [3]
    └── pkgs/ busybox/  gnuefi/  zlib/
```

The four musl cross-compilers land under `pkgs/`, where `gcc-i386-musl`
cannot be confused with anything. `gcc-11.4.0` appears once, as a
coordinate beside gtest, where it correctly reads "needs ubuntu-22.04
and this C++ ABI". Six `gcc/<ver>` coexist in one stack. Five of our
own stacks would be siblings of `gcc-14.4.0` — multiple stacks need no
new level.

## Board in `<env>` from day one

`BOARD` currently gates whether a package installs
(`Package#board_supported?`) but never appears in any install path:

```ruby
def final_install_root
  … TC / "gcc-#{a.gcc_ver}" / a.name      # no board
```

That is safe today only because board-specific packages happen to have
distinct names (`uboot` for qemu-virt, `licheerv_nano_boot` for
licheerv-nano). Two boards needing the same package built differently
would overwrite each other silently. Putting the board in `<env>` now,
while every x86 value is just `pc`, closes it before it bites.

## Ruby

Two acquisition paths, one destination:

```
cache/ruby-3.4.7-ubuntu22.04-x86_64.tar.bz2    prebuilt, per-distro
cache/ruby-3.4.7.tar.gz                        generic source
   ↓ both ↓
linux-x86_64/ubuntu-22.04/any/pkgs/ruby/3.4.7/
```

The tarballs differ; the result does not. The binary was built against
ubuntu 22.04's libraries and a source build links this machine's
libssl/libyaml/libffi, so either way the result needs this distro.
Which tarball to fetch is provenance — `SourceRef`'s job — not layout.

The build tree moves to `staging/` like every other package's, which
also removes the `ruby-3.4.7/` and `source/` directories currently
sitting beside the install and making Ruby look like a package with
three versions.

Ruby cannot be portable: it would have to be built inside our stack,
but the stack is built by pkgmgr, which needs Ruby. It is distro-bound
by necessity, not by choice. Bash still performs the first install —
it is what runs pkgmgr — but writes to the canonical path, and pkgmgr
registers a `host_ruby` package pointing at it, so it is visible to
`-l` and `--upgrade` rather than being invisible state.

## The rule that has to hold for ten years

> **New axes become VALUES, never LEVELS.**

Checked against the cases that would otherwise force toolchain6:

| case | absorbed as |
|---|---|
| libc variants | `<stack>` = `gcc-13.3.0-musl` |
| debug / LTO / sanitizers | `<stack>` = `gcc-14.4.0-lto` |
| board variants | `<env>` = `qemu-virt` |
| macOS, FreeBSD hosts | `<machine>` and `<env>` values |
| Homebrew-dependent packages | `<env>` = `macos-14-brew` |
| Canadian cross | nothing: placement is by runs-on and by |
|                | built-by, and the build machine is neither |
| same package host and target | different `<machine>` |
| a whole Linux distro on the host | more packages in a stack: the |
|                | sysroot already IS a filesystem root |

### The escape hatch, pre-approved

If something genuinely needs a fourth coordinate, do not add a level.
Collapse `<stack>` to an opaque id with a manifest inside it:

```
linux-x86_64/any/own-14/{ stack.conf, sysroot/, pkgs/ }
    stack.conf:  cc=gcc-14.4.0  libc=glibc-2.41  opts=lto
```

That is a rename plus writing manifests, not a restructure, and it can
absorb any number of future axes.

## Build identity: what makes an install "current"

The layout says WHERE a package goes. It does not say whether what is
there was built from the sources we have now — and today nothing does:

```
$ ls scripts/patches/libglycin/2.2.alpha.7/
0001-no-sandbox-outside-usr.diff
$ ./scripts/build_toolchain -s host_libglycin
INFO: All requested packages are already installed
```

That patch changed the artifact's behaviour completely, and pkgmgr
still called the unpatched build current. The same thing happened with
gdk-pixbuf, where `-Dbuiltin_loaders=png` and `png,glycin` are
different artifacts at one path — and the wrong one shipped a QEMU
that could not decode an icon while reporting success.

"Installed" must mean *built from these sources with these flags*, not
*a directory with this name exists*.

### The unit is a STEP, not a flag

"Configure flags plus build flags" is not general enough. micropython
builds two components in one package:

```
mpy-cross    cwd=mpy-cross   UNSET CC CXX AR NM RANLIB CROSS_PREFIX
                             CROSS_COMPILE
                             make V=1 -j        (+ Darwin CFLAGS_EXTRA)
unix port    cwd=ports/unix  make submodules
                             LDFLAGS_EXTRA=-static
                             make V=1 MICROPY_PY_FFI=0 …  (+ Darwin
                             UNAME_S=Linux)
```

Two directories, env deletions AND additions, make variables,
OS-conditional arguments, two invocations in one component. The
general unit is a step — `{dir, env changes, argv}` — and a package is
an ordered list of them.

### Two ways to get a fingerprint, neither of which can be forgotten

**A declarative recipe**, for builds that are a command sequence. The
flags become data that the helper both EXECUTES and RECORDS, so there
is no separate "remember to publish the flags" step to omit:

```ruby
def build_steps(ver) = [
  Step.new(dir: "mpy-cross", unset: %w[CC CXX AR NM RANLIB],
           argv: ["make", "V=1", "-j$PAR"]),
  Step.new(dir: "ports/unix", argv: ["make", "submodules"]),
  Step.new(dir: "ports/unix", env: { "LDFLAGS_EXTRA" => "-static" },
           argv: ["make", "V=1", "MICROPY_PY_FFI=0", "-j$PAR"]),
]
```

**A code fingerprint**, for the genuinely imperative tail: hash the
method's own source with comments stripped. No rewrite, no discipline,
and narrow — it hashes `build_unix_port`, not the file. Verified with
Prism on Ruby 3.4:

```
fingerprint          : 9ce6678edeb2cf81
after a comment edit : 9ce6678edeb2cf81   unchanged, no rebuild
after MICROPY_PY_BTREE=0 -> 1
                     : 46e7f55fed70f9f3   rebuild
```

Surveying every `install_impl_internal` in the tree splits 22 / 33:

| shape | count | examples |
|---|---|---|
| helper call + args array | 22 | every stack package: glib2, gtk3, qemu … |
| imperative | 33 | host_gcc (109 lines), ncurses (82), tcc (78), uboot (7) |

So the 22 get a precise recipe almost for free, and the 33 are
protected immediately by the code fingerprint, to be converted
opportunistically rather than urgently.

### What is recorded

`.build_inputs`, in the install directory beside the `.install_origin`
that is already there. Paths are normalised to `$TC`, `$SYSROOT` and
`$STAGING`, and the parallelism to `-j$PAR`, so the record is stable
across machines and still diffable by eye:

```
recipe   sha256:9ce6678edeb2cf81
argv     meson setup build --prefix=$SYSROOT/usr --libdir=lib
         --buildtype=release --wrap-mode=nofallback -Dpng=enabled …
patches  0001-no-sandbox-outside-usr.diff sha256:…
files    other/busybox.config sha256:…
```

The FULL argv is recorded, not only the package's own flags. That is
what catches changes made in the shared helpers: adding
`--wrap-mode=nofallback` to `meson_stack_build` altered dependency
resolution for all 22 meson packages at once and rebuilt none of them.

`build_files` covers inputs whose CONTENT matters rather than their
name: the patch set for that version by default — the base class
already applies those, so it already owns that knowledge — with
busybox and u-boot adding their config files. Note that busybox
already writes `.last_build_config` on every build and nothing has
ever read it; this is that idea, finished and made general.

### Where it is checked

`--check-for-updates` reports a stale package exactly as it reports a
version bump today, so **CMake needs no change at all**: it already
stops the build on exit code 2.

```cmake
COMMAND ${PKGMGR_RUBY} ${PKGMGR_MAIN} -q --check-for-updates
if (_upgrade_rc EQUAL 2)
   message(FATAL_ERROR "Some installed toolchain packages need upgrading…")
```

`-l` shows `stale` instead of `installed`, so the condition is visible
without starting a build.

## Non-goals, decided

**Content addressing.** An install is identified by (machine, env,
stack, name, version) and not by a hash of its inputs, so paths stay
legible — which is the property this whole schema exists for. The
build-identity mechanism above gives detection instead: pkgmgr reports
"installed, but built from different inputs" rather than making the
stale artifact unreachable. That is a weaker guarantee than Nix's and
a deliberate trade.

**A bootable system.** The schema covers a package set. A system also
needs state — `/etc`, `/var`, users — which is not packages and would
need its own concept if it is ever wanted.

## Open items

1. `noarch/any/any/pkgs/` — three segments, none of which vary. Kept
   for uniformity; the only place the schema costs readability.
2. `any` becomes a reserved word: no distro, board or stack may be
   called it.
3. Migration is a full rebuild, since every path change moves every
   RPATH and ELF interpreter — hence a new `toolchain5/` root rather
   than an in-place migration. `cache/` is symlinked from toolchain4
   so that nothing is downloaded twice; toolchain4 keeps working until
   it is deleted.
4. Starting empty means there are no installs without `.build_inputs`,
   so build identity applies from the first package with no legacy
   case to tolerate.
