# toolchain5: three coordinates, and why they should last

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

## Non-goals, decided

**Content addressing.** An install is identified by (machine, env,
stack, name, version) and NOT by its inputs, so two builds of one
version from different inputs occupy the same path. Demonstrated:

```
$ ls scripts/patches/libglycin/2.2.alpha.7/
0001-no-sandbox-outside-usr.diff
$ ./scripts/build_toolchain -s host_libglycin
INFO: All requested packages are already installed
```

— the patch changed the artifact's behaviour completely, and pkgmgr
still considered the unpatched build current. Nix solves this by
hashing every input into the path; that makes paths opaque, which
destroys the property this whole schema exists for.

**Mitigation instead**: record the inputs *inside* the install — patch
names and hashes, configure arguments, resolved dependency versions —
and compare on the next install, so pkgmgr reports "installed, but
built from different inputs" rather than "already installed".
Detection without content-addressed paths.

**A bootable system.** The schema covers a package set. A system also
needs state — `/etc`, `/var`, users — which is not packages and needs
its own concept if it is ever wanted.

## Open items

1. `noarch/any/any/pkgs/` — three segments, none of which vary. Kept
   for uniformity; the only place the schema costs readability.
2. `any` becomes a reserved word: no distro, board or stack may be
   called it.
3. Migration is a full rebuild, since every path change moves every
   RPATH and ELF interpreter. Hence a new `toolchain5/` root rather
   than an in-place migration; toolchain4 keeps working until deleted.
