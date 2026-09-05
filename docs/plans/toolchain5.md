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

### Tokens: naming a value instead of reading it

A step's argv refers to the install directory, the sysroot, the build
parallelism and the source's git ref through tokens -- `$INSTALL`,
`$SYSROOT`, `$PAR`, `$SRC_REF` -- resolved when the step runs.

`$SRC_REF` is the one that shows why they exist. Its value lives in
`.ref_short`, written beside the extracted tree by the cache when it
clones, so it can only be read once the source is there. A recipe that
READ it would hash differently depending on whether a source tree
happened to be present, and on which one -- the same defect that once
made glycin report stale from a build directory and fresh from the
repository root. Naming it leaves the literal `$SRC_REF` in the
digest, which is the honest record: the value is a property of the
pinned source, and the version already identifies that.

A package that names it and has no git source is told so, rather than
handed an empty string.

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

### Three states, and why the third is reported

An install's record reads as one of:

| state | meaning |
|---|---|
| `ok` | built from the sources we have |
| `changed` | built from something else |
| `unknown` | no record at all |

`unknown` is REPORTED rather than assumed benign. toolchain5 starts
empty, so every install is made by this mechanism and a record that is
absent means something went wrong. Two real cases were invisible while
a missing record counted as fine: expat's record write raised midway
through the first rebuild, and gnuefi wrote a record for one of the
three architectures it installs. Both had the same remedy as
`changed`, which is why they report the same way.

An install directory created by hand therefore reads as stale rather
than installed. That is the intended answer: nothing says what it was
built from.

A package that installs into several places is recorded in ALL of
them, not just whichever `find_install` returns first -- gnuefi builds
for i386, x86_64 and noarch from one call.

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

## The builds that are not command sequences

Where a build IS a command sequence it is data -- an ordered list of
`Step(log, argv, dir:, env:, unset:)` that the runner both executes
and records, so no command can go unrecorded. The rest define their
own `install_impl_internal` and are protected by the code fingerprint
instead, which is correct and narrow but coarser: any change to the
method is a rebuild, where a declared recipe would say exactly WHAT
changed.

### Done

Every conversion below was verified against the ARTIFACT, not by
reading the diff: rebuild, then compare byte for byte with what the
old code produced.

| package | was | is | verified |
|---|---|---|---|
| `lua` | 2 gsubs + make | 2 patches + a Step | lua, luac identical on 3 boards |
| `host_ninja` | bootstrap + FileUtils.cp | 3 Steps | ninja identical |
| `fbdoom` | 2 gsubs in patch_sources | 2 patches | binary identical (the .gz is not: gzip stores the mtime) |
| `acpica` | a substitution table + its own applier | 1 patch | acenv.h identical, kernel builds |
| `uboot` | scriptaddr gsub | 1 patch | u-boot.bin differs in 5 bytes, all inside the embedded build timestamp |
| `licheerv_nano_boot` | 2 gsubs | 1 patch | bl2_main.c identical |
| `tcc` | 51 lines: a githash read from the source, env juggling, a hand-rolled existence check | 3 Steps + the `$SRC_REF` token | tcc identical on i386 and riscv64, version string unchanged |
| `gnuefi` | a substitution table + its own applier | 1 patch | efibind.h both arches, libgnuefi.a, libefi.a identical |
| `ncurses`, `host_ncurses` | one applier, two callers wanting different sets | 1 + 2 patches | all 142 target and 109 host objects identical (only ar's container metadata differs) |
| `lcov`, `libmusl`, `freedoom`, `host_sophgo_tools` | `install_impl_internal = true`, in two spellings | `nothing_to_build?` | extraction only |

Each patch was also applied to the pristine tarball and diffed against
what the gsub produced, before being committed. Keeping the result
byte-identical -- leftover spaces in lua's `SYSLIBS` included -- is
what makes the artifact comparison meaningful.

The patches are a behaviour change, and the right one: `patch(1)`
fails loudly where a gsub that matches nothing succeeds silently.
fbdoom's homedir substitution only *warned* when it found nothing, so
an upstream rename would have produced a binary writing to a directory
Tilck does not mount, and said so once in a log nobody reads.

### Fixed: a patch directory belongs to one package

`apply_patches` keyed the directory on `pkg_dirname`, which names a
SOURCE DIRECTORY and is shared on purpose. Three pairs shared one:

```
zlib     -> zlib, host_zlib            (different versions today)
gnuefi   -> gnuefi_src, gnuefi         (SAME version)
ncurses  -> ncurses, host_ncurses      (SAME version)
```

An install path is disambiguated by its coordinates; a patch path has
none, so a patch dropped in was applied to both packages and recorded
among both packages' build inputs, with nothing said:

```
$ echo ... > scripts/patches/ncurses/6.5/0001-host-only.diff
$ ./scripts/build_toolchain -q --check-for-updates
NEEDS_REBUILD host_ncurses ncurses          # the TARGET picked it up
```

Keyed on the package name now, prefixed with the machine class it is
for -- `host_glib2`, `target_lua`, and a bare name for noarch, which
cannot collide with either. The prefix is added when missing rather
than assumed: the musl cross-compilers are host packages named
`gcc-<arch>-musl`.

`gnuefi` and `ncurses` were converted once this landed; their patch
sets stay apart from the packages they share sources with.

Converting them turned up a second hole in the same mechanism.
`parse_defs` built one flat hash per FILE keyed by method name, so two
package classes in one file shared a namespace and the later
definition won: ncurses.rb held a single `install_impl_internal` and
it was the HOST package's, so `class_source(NcursesPackage)` returned
the host's build. The target's recipe could change without changing
the target's digest -- a MISSING rebuild, not a spurious one. Defs are
keyed by class now, and `method_source` refuses an ambiguous name
rather than picking. A test asserts across the registry that no class
borrows a neighbour's method; it found `gnuefi_src` and
`host_libglycin` doing exactly that.

gnuefi additionally replaces the base class's `install_impl` wholesale
-- it extracts the tarball once per arch -- and that is where patches
are applied, so the conversion silently applied none until
`apply_patches` was wired into its own loop.

### Genuinely not command sequences

  * **A build that verifies its own output.** `host_gcc` writes a
    specs file and then compiles a test program to prove the compiler
    emits portable binaries by default; `host_glibc` and
    `host_binutils` are the same shape. That is a POST-INSTALL
    ASSERTION and deserves its own concept rather than being flattened
    into a step list. The specs file is also generated from gcc's own
    output, so it cannot be a patch.

  * **Supply-then-repair a config.** `busybox` and `uboot` copy a
    checked-in `.config` in, build, and normalise the file afterwards.
    The fixup could move into the config file itself; until it does,
    the fingerprint is the honest answer.

  * **File operations interleaved.** `vim`, `ncurses`, `host_qemu`,
    `host_meson`, `host_mconf`, `host_gtest`, `gnuefi` and `fbdoom`
    mix mkdir, cp, symlinks or DESTDIR juggling between commands.
    `host_mconf` and `host_meson` additionally compute their argv from
    `deps_build_env`, which resolves install paths and raises when a
    dependency is absent -- not something a staleness check can call.

  * **Renames over a blob.** The four `gcc-*-musl` packages rename
    every file in `bin/`; `tfblib` symlinks itself into the source
    tree. Neither is a build.

A `Step` variant carrying a lambda would let a file operation sit in
the list without pretending to be a command, but a lambda cannot be
fingerprinted as data -- it falls back to hashing the method that
defines it, which is where those packages already are. It buys
uniform ordering and logging and nothing else.

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
   case to tolerate -- which is why a missing record is reported as
   `unknown` rather than waved through.
5. 25 packages remain on the code fingerprint rather than a declared
   recipe, from 32 -- see "The builds that are not command sequences"
   above for why each one is where it is. The rest are genuinely not
   command sequences.
6. gnu-efi's three per-arch headers do not say the same thing, and the
   one patch over them means something different in each. Nothing
   built today compiles the affected header; see
   docs/plans/gnuefi-arch-asymmetry.md for what it is and the options.

Closed: the artifacts that embed a build timestamp -- `fbdoom.gz`,
`u-boot.bin`, the ncurses archives -- are not a problem. Only the
timestamps differ and nothing reads them: the gzip payload, the ar
members (142 target, 109 host) and all of u-boot but five bytes of a
printed banner are identical. It costs a byte-for-byte comparison
between rebuilds, which is a verification convenience and not a
property of the toolchain.

---

# What was executed (2026-08-29 → 2026-09-03)

Everything above is the plan. This is the record of carrying it out:
the packages that were built, every logic bug the work hit with the
command that showed it, and the five instruments that now stand
between the package manager and the next one.

The short version: the schema held. Not one bug below was a bug in
the three coordinates. Every one was a question about a specific
installation answered from ambient state instead of from that
installation's coordinates -- the same bug, sixteen times, in
sixteen places that each had to be found.

## 1. The QEMU matrix, and what building it cost

Six QEMU majors now exist, each built by a compiler from its own
time, each in its own stack:

```
linux-x86_64/any/gcc-11.5.0/pkgs/qemu/6.2.0/     gmp 6.1.0, mpfr 3.1.6
linux-x86_64/any/gcc-12.5.0/pkgs/qemu/7.2.0/
linux-x86_64/any/gcc-13.4.0/pkgs/qemu/8.2.0/
linux-x86_64/any/gcc-14.4.0/pkgs/qemu/9.2.0/
linux-x86_64/any/gcc-15.3.0/pkgs/qemu/10.2.0/
linux-x86_64/any/gcc-16.2.0/pkgs/qemu/11.1.0/
```

`-s host_qemu:6.2.0` pins GCC 11.5.0 and, through it, gmp 6.1.0 and
mpfr 3.1.6, and builds that whole world. Compiling a 2021 QEMU with a
2025 compiler tests neither of them. 338 installs, all `ok`.

Package-side problems hit on the way, each fixed where it belonged:

| symptom | cause | fix |
|---|---|---|
| `found no usable distlib` (QEMU 8+) | mkvenv needs distlib to populate the venv it creates | it was a build requirement, so the base system list -- then withdrawn, see below |
| 3113/3351 targets, then `xkbcommon: Failed to add any default include path` | `pc-bios/keymaps/meson.build` regenerates every keymap when xkbcommon is found, which needs xkeyboard-config's data | install the keymaps the tarball ships; upstream generated them from that same data |
| built 3316/3316, then `Build directory has been generated with Meson 1.11.1, incompatible with 1.12.0` | QEMU 11.1 creates a pyvenv and installs the meson its `pythondeps.toml` pins; `meson install` was the one step run by a different tool than the rest | `ninja install`, which runs the rules the generating meson wrote |
| `--disable-glusterfs` refused | the option was removed upstream | `configure_flags(ver)`: options belong to a version, not to the package |

### The interpreter is a package, not whatever PATH finds

Two machines, two failures, neither fixable by installing a system
package:

```
/usr/bin/python3      distlib yes, tomllib NO    (3.10)
brew python3          distlib NO,  tomllib yes   (3.14)
```

QEMU 8+ needs both. The build was not *choosing* between them, which
is the actual defect: every other input to a Tilck build comes from
somewhere we chose. `host_python` is now a package -- a prebuilt
CPython 3.11.16 from python-build-standalone, with distlib installed
from a cached wheel -- and the `$PYTHON` token names it. The system
dependency was withdrawn.

And, so that a missed call site cannot go quiet, `scripts/pkgmgr/shims/`
holds a `python3` (and a `python`) that prints what went wrong and
exits 127. They sit on PATH *behind* what dependencies publish, so a
correctly declared build never sees them. Deliberately no fallback:
falling back to `/usr/bin/python3` is the behaviour being prevented.

## 2. Every logic bug, with the command that showed it

Sixteen, in the order they were found. Each row is a real invocation.

| # | command | did | should | cause |
|---|---|---|---|---|
| 1 | `-l` after building 44 packages into gcc-16.2.0 | called 22 of them stale, ten minutes old | `ok` | the recipe names its own sysroot; judged at the current stack, not the install's |
| 2 | `-f -s host_qemu:6.2.0` | "Force-removing", then "already installed" | rebuild | force_remove ran before the stack was resolved: removed a tree nobody was rebuilding |
| 3 | `-f -s host_qemu:6.2.0` (again) | same, still | rebuild | `nil` passed for the compiler; the filter reads nil as "must BE nil", true only of a noarch package |
| 4 | `-H 14.4.0 -u host_qemu:6.2.0` | nothing removed, nothing said | remove the 14.4.0-stack tree | `default_cc == "syscc"` as a proxy for "is a host package", false since `:stack` packages started answering with their own GCC |
| 5 | `-u zlib` on riscv64 | removed qemu-virt AND licheerv-nano | the board you are on | matched `e.arch == arch`; an arch is two thirds of a coordinate |
| 6 | `-f -s host_gcc:16.2.0` | removed all six GCC majors | remove 16.2.0 | "every version of the package" where "this version" was meant |
| 7 | `--check-for-updates` | latent: two boards of one arch judge each other | judge at the install's board | `with_install_context` scoped arch and stack, never the board |
| 8 | `-a riscv64 -f -s uboot` | `requires board qemu-virt` -- *after* deleting it | rebuild | `board_supported?` read the global `BOARD`, not the board of the arch being built for |
| 9 | `--print-layout` under `-a` | could disagree with the package manager | agree | `Layout.board_of` was the board rule's second copy, and only one copy learned about scopes |
| 10 | `-d` with no mode | installed the default packages | nothing | the one mode that never checked `-d` |
| 11 | `-s ub:1.0.0 -f` on the wrong board | deleted the install, then refused to rebuild it | refuse first, touch nothing | the board was checked inside the install, after `-f` |
| 12 | `-u ALL` | only target packages of the current arch and compiler | everything for this scope | the arch and compiler filters were filled in from defaults |
| 13 | `-u multi` where the default is elsewhere | decided by whether the default is installed *anywhere* | decided by what is at THESE coordinates | -- |
| 14 | `-C <pkg> -d` | reconfigured | nothing | `-C` ignored `-d` |
| 15 | `dep_closure` on a cycle | returned an answer, silently | name the cycle | `seen` swallowed the second visit; detection ran once at startup, on the version-less graph |
| 16 | six CI runs | `host_python is not installed` | pass anywhere | two tests read the developer's toolchain |

Bugs 1, 7 and 9 are one sentence with the word changed: *the recipe
is judged at the coordinates of the invocation rather than of the
installation*. Bugs 2-6 and 12-13 are another: *the set an operation
acts on is computed from a partial key*.

## 3. The three things the bugs were made of, removed

**Ambient reads.** `ARCH`, `BOARD` and the current stack were read in
27 places. They are read in six now -- their definitions, the CLI
boundary, and the two accessors that own the answer -- and a lint
(below) fails the suite on a twenty-eighth.

**A second identity mechanism.** `Coords` was introduced as *the*
identity of an installation and the migration was never finished:
`uninstall` still matched a tuple of `(ver, compiler, arch)` with
`Coords` bolted on as a fifth clause. `InstallSelector` is now the
whole key as one value -- a name, a version, and a union of coordinate
filters -- and `uninstall_selector` is the single function that turns
a command line into one, with the coordinates written out per kind of
package so a reader can check each line against the layout table. The
lint bans `.arch ==` and `.compiler ==` outside it; the pin that
tracked the remaining sites is empty.

**A scope with one coordinate missing.** `with_target_coords(arch,
board)` moves both together, because a board is only meaningful for an
arch. `PackageManager#board_for` is the one place that knows which
board applies to which arch.

## 4. Five instruments

Each answers a question the others cannot. Together they are why the
next bug of this class has to get past all five.

**The lint** — `tests/lint/ambient.rb`, 157 lines, Prism. Three rules:
a read of ambient state outside its owners (R1); an identity
comparison on part of a coordinate (R2); a write to a scope variable
outside its `with_*` (R3). Prism and not grep, so `"ARCH=x86"` handed
to make is not a read and a comment is not a read. The allowlist lives
in the test with a reason per entry, and an entry naming a method that
no longer exists fails. It self-tests on planted violations: a lint
that cannot see one reports nothing, and nothing reads as clean.

**The model** — `tests/model/model.rb`, 794 lines. The contract as a
program: a world is a set of `(name, version, coords, record, origin)`,
and every command line is a pure function from (registry, world,
invocation) to (exit code, world', output). No I/O, no packages. It is
validated before it judges: every historical bug is a case with the
answer written by hand, and its own laws hold (`select` is total, dry
runs change nothing, transitions are deterministic). Five places say
SPEC, where the model states what is right and the implementation was
made to agree.

**The laws** — `tests/laws.rb`, applied by `TestHelper#run_cli` around
every `Main.main` the suite drives. L1 the world after equals the
model's; L2 `-d` changed nothing; L3 every install is where its
package says, judged at its own coordinates; L4 everything installed
has an `:ok` record. A test about `-l` output is thereby also a test
that `-l` changed nothing. About 1,040 command lines are judged per
run, and the runner prints how many fell outside the model's grammar,
so a suite that quietly stopped checking would say so.

**The exhaustive lane** — `tests/exhaustive/`. Fifteen registry shapes
(one feature each), every world of at most two installations, every
context, every command line in the grammar. Two is not arbitrary:
every bug in the table manifests with two of something, and a fixture
with one cannot tell "the right one" from "the only one". The wide
domain ran once at **318,864 cases, 0 disagreements**; the lane in CI
is 66,426, since the multi-package shapes enumerate dependency
structure rather than coordinates the single-package shapes already
cover. It self-tests first (a snapshot equals a second snapshot, a
world reads back as built, a planted disagreement is seen) and refuses
to run otherwise. `--case ID` replays any failure.

**Mutation** — `-t --mutation`, or `scripts/dev/claude/pmmutate`. Every
site of the logic core is made wrong one way -- a comparison flipped,
a conjunct dropped, a guard deleted, `nil` for `"ALL"`, a scope not
opened or not restored -- and the suite must fail. The unmutated suite
must pass first, and its time sets the per-mutant timeout. A survivor
is a test that does not exist; a *timeout* is a walk without a bound,
which is a defect in the code, and fails the run too.

The first run: **456 mutants, 116 survivors.** Each was answered by a
test (36 of them, `tests/test_survivors.rb`), by deleting a line that
said something the code already knew, or by an annotated reason
(`# mutation: equivalent -- <reason>`, checked to still sit on a
site). Among the deletions: `Coords#own_env?`, dead, five of whose
mutants survived because nothing could have noticed.

Current: **362 mutants, 362 killed, 0 timed out, 0 survived**, in CI.

### What the lane and the mutants found that the tests had not

- the default install ignored `-d` (bug 10);
- the board was checked after `-f` had removed the tree (bug 11);
- `-u ALL` meant far less than anyone typing it means (bug 12);
- `-u` with no version decided by what is installed elsewhere (13);
- a dependency walk swallowed cycles and could not be bounded (15):
  two existing tests asserted the silent behaviour. Every walk now
  names a cycle with its path (`a -> b -> c -> a`), at whatever
  version it appears, and is bounded exactly -- once per root plus
  once per edge -- so nothing in the resolver can hang.

## 5. The host world is x86_64 Linux, and says so

`host_gcc` and `host_qemu` are the roots of a *host world*: our GCC
with its glibc sysroot, the QEMU matrix built by it, and the fifty
packages nothing else needs. It is a Linux userland by construction
and has been exercised on x86_64 only. On macOS all 53 were listed as
installable and `-s host_qemu` failed deep inside `host_linux_headers`.

The roots declare where they run and `host_supported?` derives the
rest: a package in `host_world_names` -- reachable from a root and
from nothing else -- is supported only where every root is. Fifty
packages hidden by two declarations and one rule, and a package both
the world and Tilck need stays visible. Elsewhere they are not listed,
`-s` is refused at the door with the reason, and `-L` says there are
no stacks. Where a package runs is a `NON_RECIPE_HOOK`: declaring it
in the constructors made twelve installs read as changed for a
statement no build step reads.

## 6. CI

`.github/workflows/ci-pkgmgr.yml`, "Package manager", on a stock
Debian image: `tests` (`-t --exhaustive`) and `mutation`
(`-t --mutation`), on every push touching the package manager, on
pull requests, and by dispatch, one run per branch at a time. The six
toolchain workflows run `-t --exhaustive` before they build anything.

First green run of the pipeline: `tests` 5m59s, `mutation` 42m0s,
362/362 killed.

## 7. What this proves, and what it does not

Proved mechanically, every commit: for the catalogue of shapes and
worlds of at most two installations, the implementation's effect on
the tree and its answers equal the model's; the implementation reads
its inputs only through their owners; every line of the logic core is
defended by a test that fails if it is wrong.

Not proved: worlds of three or more (the bound rises when a bug ever
appears there -- none has), shapes outside the catalogue (add one when
a package with a new feature appears), and anything about the real
recipes or the network. A package that fails to build because upstream
changed is not a logic bug and is not what any of this is for.

The standing rule, in CLAUDE.md: **a pkgmgr logic bug is not fixed
until its mutant dies.** The model says the right answer or is
corrected first; a shape or a command line is added so the lane fails
before the fix and passes after; and the mutant that reproduces the
bug must be expressible and killed.
