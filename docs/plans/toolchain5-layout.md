# toolchain5: a layout with two explicit axes

## Why toolchain4's layout stopped working

`gcc-*` currently means four unrelated things, depending only on where
it appears:

```
toolchain4/gcc-13.3.0/i386/busybox/1.36.1/         a cross-compiler VERSION
toolchain4/host/…/portable/gcc-i386-musl/13.3.0/   a PACKAGE (the compiler)
toolchain4/host/…/portable/gcc-14.4.0/glib2/…      a BUILD-COMPILER axis
toolchain4/host/…/ubuntu-22.04/gcc-11.4.0/gtest/…  the host C++ ABI slot
toolchain4/host/…/ubuntu-22.04/gcc/14.4.0/         a PACKAGE (our host_gcc)
```

The third of those is new and is the mistake. `host/` had one meaning —
*built with the host compiler* — and putting packages built with **our**
compiler inside it broke that, at a level where the directory name is
indistinguishable from a package name. `PackageManager#compiler_slot?`
exists solely to guess which is which:

```ruby
# a slot is "gcc-" followed by something that parses as a version;
# gcc-i386-musl is a package, gcc-14.4.0 is a directory of packages
```

A predicate that has to disambiguate two meanings of one name is the
symptom, not the fix.

## The two axes

Every installed artifact answers exactly two questions, and today the
tree encodes them inconsistently — the target side puts the toolchain
first and the environment second, the host side names only the
environment and assumes the toolchain.

**RUNS-ON** — what a machine must provide to execute this artifact.
This is what decides *consume or rebuild* for someone sharing the
toolchain, and it is a chain of increasing specificity:

| runs-on | means |
|---|---|
| `linux-x86_64` | any machine of this OS and arch |
| `linux-x86_64+ubuntu-22.04` | …that also runs this distro |
| `linux-x86_64+ubuntu-22.04+gcc-11.4.0` | …and provides this C++ ABI |
| `tilck-i386` | a Tilck i386 system |
| `noarch` | nothing; data |

**BUILT-BY** — the toolchain that produced it.

| built-by | means |
|---|---|
| `host-cc` | the host's own compiler, whatever it is |
| `gcc-14.4.0` | a compiler we built, running on the host |
| `gcc-13.3.0` | a cross compiler we built |
| `none` | no compiler was involved |

They are independent. The C++ ABI appears under RUNS-ON rather than
BUILT-BY because it is a *runtime* requirement: gtest built by the host
compiler needs that ABI at link time in its consumers, which is a
property of where it can be used, not of who made it.

## The layout

```
toolchain5/<runs-on>/<built-by>/<pkg>/<ver>/
```

Four levels, always, with a fixed meaning each. Level 1 is always an
environment, level 2 always a toolchain, level 3 always a package,
level 4 always a version.

```
toolchain5/
  cache/                                             tarballs (not installs)
  staging/                                           in-progress builds
  sysroots/<runs-on>/<built-by>/                     composed views

  noarch/none/acpica/20240927/
  linux-x86_64/host-cc/gcc-i386-musl/13.3.0/
  linux-x86_64/host-cc/gcc-riscv64-musl/13.3.0/
  linux-x86_64/gcc-14.4.0/glib2/2.88.3/
  linux-x86_64/gcc-14.4.0/gtk3/3.24.52/
  linux-x86_64/gcc-11.5.0/glib2/2.88.3/             a second stack, side by side
  linux-x86_64+ubuntu-22.04/host-cc/mtools/4.0.49/
  linux-x86_64+ubuntu-22.04/host-cc/gcc/14.4.0/     our gcc; system cc built it
  linux-x86_64+ubuntu-22.04+gcc-11.4.0/host-cc/gtest/1.17.0/
  tilck-i386/gcc-13.3.0/busybox/1.36.1/
  tilck-riscv64/gcc-13.3.0/busybox/1.36.1/
```

The fixed depth is what removes the ambiguity. A package name can only
ever appear at level 3, so `gcc-i386-musl` is unmistakably a package
and `gcc-14.4.0` at level 2 is unmistakably a toolchain.
`compiler_slot?` is deleted rather than refined.

## What this fixes

**The tier rule survives intact, and generalises.** RUNS-ON *is* the
tier, readable straight off level 1: no `+` means it runs anywhere,
`+distro` means rebuild elsewhere, `+distro+cc` means rebuild elsewhere
and under that ABI. The portability audit keeps enforcing the
declaration; it just now has a name to enforce it against. Target
packages get the same treatment for free, where before they had a
different shape.

**Multiple stacks are ordinary rather than special.** Two stacks are
two BUILT-BY values under the same RUNS-ON, needing no new level and no
new concept:

```
linux-x86_64/gcc-14.4.0/glib2/2.88.3/
linux-x86_64/gcc-11.5.0/glib2/2.88.3/
sysroots/linux-x86_64/gcc-14.4.0/
sysroots/linux-x86_64/gcc-11.5.0/
```

**`host/` stops lying** by ceasing to exist: there is no directory
claiming "built with the host compiler", because BUILT-BY says so
explicitly at every path.

**The sysroot gets a home.** It belongs to exactly one (runs-on,
built-by) pair, which is what a stack *is*, so it is addressed by the
same two coordinates as the packages it views.

## Cost

Every path changes, so every RPATH and ELF interpreter changes, so it
is a full rebuild of everything — which is the reason for a new
`toolchain5/` root rather than a migration. toolchain4 keeps working
until it is deleted.

## Open questions

1. **Separator.** `+` reads well and is shell-safe unquoted. `%` and
   `~` are alternatives. Nested directories were rejected because they
   make the depth vary (2, 3 or 4 segments), which is exactly the
   ambiguity being removed.

2. **`host-cc` as a literal.** It says "whatever compiler this host
   has", which is honest for BUILT-BY but records nothing. The actual
   version could be recorded in install metadata instead, so a change
   of system compiler is *detectable* without being part of the path
   (it is not part of RUNS-ON unless C++ is involved).

3. **`noarch/none/`.** Uniform, but reads oddly for a source tarball
   that no compiler touched. The alternative is a special two-level
   case for noarch, at the cost of the invariant that every install
   path has exactly four levels.
