# How often a library breaks a prebuilt binary without changing its
# soname

A research report, consolidated from four investigations (each a
file under `docs/plans/research/`, ~2,850 lines with primary sources:
glibc's own NEWS and abilists, GCC's version scripts, project NEWS
files and bug trackers, Arch's rebuild lists, Debian's UDD bug
database, abi-laboratory.pro's ABI Tracker). Date: 2026-09-17.

The question it answers is an input to item B of
`pkgmgr-schema-longevity-review.md`: the package manager tags every
binary that links the host distro's libraries with the distro's
`ID-VERSION_ID`, and treats any change of that string as "rebuild
everything". On a rolling distro the string changes on every point
release (Omarchy 4.0.3 to 4.0.4 stranded 26 installs on 2026-09-15,
all of which still ran). The alternative under consideration is to
keep a binary while every library it needs (its `NEEDED` sonames)
still resolves on the machine. That test is blind to a library that
changes ABI or behaviour without changing its soname, and the
question was: how often does that actually happen?

## 1. The answer in one paragraph

For the ordinary C libraries a toolchain links from the distro, a
same-soname break is rare, deliberate ones are caught by the soname
bump convention nearly always (19 of 20 libraries examined), and the
exceptions cluster in four libraries (libxml2, libpython, expat,
openssl). For glibc and libstdc++ the picture inverts: the soname has
not moved in twenty years by design, the ABI contract is kept
rigorously in the direction that matters (old binary on new library),
and yet glibc changed the observable behaviour of old binaries in 21
of its last 32 releases, breaking real shipped software in 10 of
them, 7 of those in the last five years. None of those ten would have
been caught by a soname check, a symbol check, or a full libabigail
ABI diff, because none of them removed or re-laid-out anything the
binary calls: they were behaviour changes (input strictness, syscall
adoption under seccomp, loader hardening). The distro version string
would not have caught them either, since it moves independently of
the libraries. The only detector for that class is running the tool.

## 2. Frequency, per library class

Classes used throughout: (a) new binary on old library, refused by the
loader's symbol-version check at load time; (b) old binary on new
library, same soname, breaks: the class a soname check cannot see;
(c) behaviour change with no ABI change at all; (d) a soname bump,
which the check sees.

### glibc (libc.so.6 since 1997), window 2.13 to 2.44, 32 releases

- Verified (b) or (c) breaks of named shipped software: 10 releases.
  2.13 memcpy backwards (Flash); 2.24 libio vtable validation; 2.28
  sticky EOF (cups-filters); 2.29 tcache double-free abort; 2.33
  faccessat2 EPERM under seccomp (containers); 2.34 clone3 EPERM
  (Docker) and GLIBC_PRIVATE removals (apitrace, Emacs); 2.35 rseq
  registration (tcmalloc, Elastic Beats); 2.36 DT_HASH dropped (Easy
  Anti-Cheat games); 2.41 exec-stack dlopen refused (Julia, Steam
  games with FMOD, MATLAB); 2.42 termios speed rework (serial tools).
- Documented in NEWS with no named victim found: 11 more releases.
- Releases with no (b)/(c) entry: 11, almost all in 2011-2015.
- Rate: one verified break every ~19 months over the window, about
  one per year in the last five. Two patterns account for nearly all
  of them: glibc adopting a new kernel interface that the program's
  sandbox rejects, and glibc removing a tolerance for undefined
  behaviour that programs relied on.
- What a soname check sees: the libcrypt split (libcrypt.so.1 vs
  libxcrypt's .so.2), once, and only at packaging level. Nothing else.

### libstdc++.so.6 and libgcc_s.so.1, GCC 5 to 15 (2015 to 2025)

- Verified (b): exactly two. GCC 7.1-7.3 switched the thrown
  std::ios_base::failure type so old-ABI binaries catching it
  terminated (fixed 7.4/8.1). GCC 13.1+ replaced libgcc_s's frame
  registration with a lock-free tree and broke JITs that call
  __register_frame (fixed piecemeal through 2025-03).
- (c): two documented, no victim found (ios_base::failure not
  catchable across the dual-ABI seam in GCC 5/6; std::string reserve
  no longer shrinking in GCC 11).
- The dual ABI (GCC 5, 2015) is the canonical "same soname, different
  ABI" event; old binaries kept working by design, and the breakage
  was between libraries built with different ABIs (Debian renamed
  ~300 packages).
- A finding specific to prebuilt distributors: the most common real
  failure is not a break but a shadowing copy, a `libstdc++.so.6`
  shipped by Steam, conda or Julia that the soname resolves to
  instead of the system's. A soname check passes; the wrong file
  loads.
- One class (a) hole: a GCC 13.1 binary on a GCC 12 libstdc++ loads
  with no version error and uses an uninitialised std::cout; 13.2
  added a version node purely so the loader would refuse it.

### The common C libraries (20 examined: zlib, zstd, ncurses, libffi,
### libxcrypt, elfutils, flex, tcl, expat, libpython, readline,
### libxml2, sqlite, bzip2, xz, gmp, mpfr, mpc, isl, openssl)

- 19 of 20 bump the soname on a deliberate break. libxml2 is the
  outlier: it removed exported symbols and changed struct layouts
  under libxml2.so.2 in essentially every feature release from 2019
  to 2024 (Gentoo masked 2.13; Debian shipped "2.12.7+really2.9.14")
  before finally moving to .so.16 in 2025.
- Update pressure on a rolling distro: Arch pushed 87 updates in 2024
  and 87 in 2025 for this set, one every four days, four packages
  (libxml2, python, elfutils, sqlite) making up half.
- Against those ~175 pushes: nine events that would affect an
  already-built binary. Two were soname bumps (caught). Two were (b)
  with a removed symbol or changed layout (libxml2 2.13, CPython
  3.13.2 PyConfig). Five were (c): openssl 3.x patch-release
  strictness and defaults, expat 2.6.0 reparse deferral, xz 5.6.
  About 5 percent per push, but 15 of the 20 libraries had zero
  same-soname incidents in two years, and over ten years zlib had one
  bad release (1.2.12, CRC strictness, broke Java, reverted in
  1.2.13), bzip2 one, readline one behaviour change (8.1 bracketed
  paste), ncurses one data-format change (6.1 terminfo) with no
  library ABI change.

### The distributions' own record

- Arch: 99, 153 and 175 rebuild lists in 2023, 2024, 2025, nearly all
  soname bumps; explicit same-soname rebuilds 0, 3 and 2 (Qt 6.6.2,
  fmt 11.1.0, libconfig 1.8, a rofi plugin ABI). pacman's soname
  dependency feature is opt-in and covers 7 percent of packages;
  rebuilds are decided by a human reading `sogrep`.
- Debian: ~140 library transitions a year (range 86-219 since 2011);
  bugs titled "ABI break without SONAME bump": 2 to 6 a year since
  2017; RC-severity ABI-break bugs 6 to 16 a year. Same-soname breaks
  are 2 to 4 percent of ABI events archive-wide, and Debian's answer
  to them is a human renaming the binary package to force a
  transition. Its per-symbol mechanism (`symbols` files) catches
  removals at build time but not layout or enum changes.
- Fedora: mass-rebuilds every release for toolchain reasons; ran
  libabigail's abipkgdiff in CI (Taskotron, retired ~2020; now
  rpminspect). No aggregate statistics are published.
- ABI Tracker on the core host-library set: 377 version steps, 29
  soname changes, 79 same-soname steps flagged below 100 percent
  compatible, mostly opaque-struct growth at 99.x percent; the glibc
  "removals" it lists are default-version-node moves with compat
  symbols kept.
- The prebuilt ecosystem's shared answer (manylinux, AppImage's
  exclude list, Valve's steam-runtime): trust a short list of host
  sonames (glibc family, libgcc_s, libstdc++, X11/GL, and explicitly
  libz, libexpat, libgmp) and bundle everything else. Valve's stated
  reason is the same-soname problem: "libraries claim to be
  compatible by having the same DT_SONAME but are in fact not".

## 3. What each candidate check sees

| check | (d) soname bump | (a) new-on-old | (b) symbol gone | (b) layout | (c) behaviour |
|---|---|---|---|---|---|
| `ID-VERSION_ID` string (today) | yes, plus every non-event | no | no | no | no |
| every NEEDED soname resolves | yes | no | no | no | no |
| plus imported symbols all defined (`ldd -r`, bind-now) | yes | yes | yes | no | no |
| plus version-node check (verneed vs verdef, what ld.so does) | yes | yes | yes | no | no |
| libabigail full ABI diff (needs debug info of both) | yes | yes | yes | yes | no |
| running the tool end to end | yes | yes | yes | yes | yes |

Three things the table makes plain:

1. The distro version string is the weakest row that is not simply
   wrong: it sees soname bumps only by accident of firing on
   everything, so it carries no information about any particular
   update. The reports' incident lists show it would have "caught"
   glibc 2.36 and 2.41 on Omarchy only if a VERSION_ID bump happened
   to coincide, which it does not (Omarchy's version moves
   independently of its glibc).
2. A soname check plus the imported-symbol check is exactly what the
   loader does with `LD_BIND_NOW=1`, costs a few milliseconds per
   binary, has no false positives, and catches every removed-symbol
   incident in all four reports (GLIBC_PRIVATE, malloc_set_state,
   libxml2, zstd experimental symbols, openssl 1.0.2g, spdlog, gsl,
   Qt 6.6.2). It is the ceiling of what a static check can do
   without debug information.
3. Everything that actually broke the core libraries our binaries need
   (glibc, libstdc++, zlib) in the last ten years is in the two
   rightmost columns, and only the last row sees it.

## 4. Implication for item B

The evidence supports a design with three layers, none of which is
the distro version string:

1. **Placement stays `ID-VERSION_ID`.** It records where a build
   happened and is the right coordinate for a fresh build. It stops
   being the test of whether an existing install is usable.
2. **Usability is judged per install by the loader's own test**: at
   build time, record each binary's NEEDED sonames and imported
   versioned symbols in `.install`; on a later run, resolve them
   against the machine (one `ldconfig -p` read, then a set difference
   per install; the file-opening cost is paid once at build). An
   install whose sonames or symbols no longer resolve is unusable
   with the missing name stated; one that resolves is usable at its
   coordinates, whatever env it sits under. This catches every
   deliberate break on record and every removed-symbol break, and it
   never fires on a compatible update, which the string does on every
   one.
3. **A smoke run per tool is the only detector for the rest**, and it
   is the same instrument the repository already demands for a
   changed package ("boot the result and exercise it"). Record the
   host's glibc version (`getconf GNU_LIBC_VERSION`) and the versions
   of the libraries each install resolved to; when one of them moves
   under an install, re-run that install's smoke test (compile, link
   and run a program with the prebuilt GCC; import a C extension with
   the prebuilt Python; build a trivial meson/ninja project) and mark
   the install `verified on <host>` or `broken: <reason>`. A rebuild
   is then a decision taken on a named failure, never an automatic
   consequence of a string moving.

Two libraries deserve a stricter rule by their own record: libxml2
(rebuild on any version change until the tree links .so.16, which
follows the convention) and libpython (patch releases have slipped
twice; our python is our own build, so this applies only to anything
we build against the host's). Neither is linked by our distro tier
today.

What no design can offer, stated plainly: a behaviour change under an
unchanged symbol is invisible to every static test the ecosystem has,
including the ones Debian, Fedora and Valve run; the distributions
handle it by hand when a bug report arrives. The layered design does
not pretend otherwise; it makes the failure visible at the first run
instead of at the fortieth minute of a build, and it stops throwing
away working installs on every distro point release.

## 5. What was not verified

Carried over from the four reports, each of which has a section
naming its own gaps: no published aggregate libabigail or Fedora CI
statistics exist; openSUSE's abichecker hit rate is unknown; the
Ponomarenko/Rubanov 2012 paper on ABI break frequency is paywalled;
several Arch GitLab issues were blocked by anti-bot protection and
were read through their todo-list descriptions and press coverage;
named victims are missing for some documented glibc changes (2.23
through 2.27, 2.30 through 2.32, 2.37, 2.39, 2.40, 2.43) and for the
libstdc++ reserve() change; the prompt's own candidate "glibc 2.37
qsort introsort" turned out never to have shipped (landed 2023-10-31,
reverted 2024-01-15 before 2.39), and no 2.41 `__libc_start_main` or
`dlopen` TLS change exists.
