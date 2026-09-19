# Same-soname ABI breaks: how distros decide rebuilds, and how often

Research note, 2026-09-17. Question under study: we tag prebuilt
host-side binaries (our GCC, Python, binutils, meson, ninja, mtools)
with the host distro's `ID-VERSION_ID` and rebuild on any change. On a
rolling distro (Arch, Omarchy) that strands the tree on every point
release. The proposed replacement, "keep the binary while every NEEDED
soname still resolves", is blind to a library that breaks ABI or
behaviour without bumping its soname. This note collects how the
distributions themselves handle that blind spot and how often it is hit.

Headline (details and sources below):

- Every distro's automatic machinery keys on the soname (Arch sodeps,
  Debian shlibs, RPM elfdeps) or on the soname plus symbol version
  nodes (RPM elfdeps, Debian symbols files). None of them detects a
  same-soname ABI break automatically at install time; all of them
  rely on a human noticing, then bumping something by hand (Debian
  package rename `libfoo1` to `libfoo1t64`/`libfoo1v5`/`libfoo1d1`,
  Gentoo sub-slot, Arch todo list).
- Measured frequency of the blind spot, per distro, is a few incidents
  a year across the whole archive: Debian has 56 bugs titled "ABI
  break without SONAME bump" in 23 years (2 to 6 a year since 2017);
  Arch's todo lists show 4 explicit same-soname rebuilds in 2024-2025
  out of 328 rebuild lists. Set against 125 to 188 Debian transitions
  a year and 99 to 175 Arch rebuild lists a year, same-soname breaks
  are roughly 2-4% of ABI events.
- The libraries our binaries actually NEED (glibc, libstdc++, libgcc_s,
  zlib, gmp, mpfr, mpc, ncurses, libffi, openssl, xz, zstd, expat,
  readline, libxml2, util-linux) are exactly the set the ecosystem
  treats as trustworthy by soname: manylinux, AppImage and Steam all
  assume them from the host, and glibc/libstdc++ have never bumped
  their soname (they evolve via symbol versions). The incidents that
  did hit real users on this set were behavioural (glibc 2.36 DT_HASH,
  glibc 2.41 execstack, zlib 1.2.12 CRC input strictness), and no
  distro-level tag would have caught them either: `ID-VERSION_ID` on
  Omarchy moves on Omarchy releases, not on glibc updates.

Omarchy note: on this host `/etc/os-release` says `ID=omarchy`,
`VERSION_ID="4.0.4"` and is rewritten by Omarchy (file dated two days
ago), while `/usr/lib/os-release` belongs to Arch's `filesystem`
package. The string we key on today tracks neither the library set nor
its ABI: glibc, zlib and openssl roll under a constant `VERSION_ID`,
and `VERSION_ID` bumps with zero library changes.

## 1. Per distro: how a rebuild is decided

### Arch Linux

Mechanism (packager side). The packager builds the new library, then
`checkpkg` diffs the list of `.so` files against the version in the
repos. If it reports a soname change, the developer wiki says a rebuild
of dependents "is required". `sogrep` queries the Arch "soname links
database" (NEEDED entries scanned from every package in the repos) to
list the consumers; the packager opens a todo list on archlinux.org
with those packages, pushes the library to `[staging]`, maintainers
rebuild into `[staging]`, everything moves through `[testing]` to
stable together. The wiki workflow, verbatim:

    If checkpkg identified a shared object name (aka. soname) change in
    package's library package.so, it is required to rebuild packages
    directly depending on it.
    $ for repo in core extra; do for lib in $(find-libprovides package
      | sed 's/=.*//g'); do sogrep -r $repo $lib; done; done | sort | uniq

Sources: DeveloperWiki:How_to_be_a_packager (sections "Run checkpkg",
"Run sogrep on identified soname change"), devtools(7), sogrep(1),
find-libprovides(1), checkpkg(1). The todo lists are public at
https://archlinux.org/todo/ (1412 lists, 2013 to today; 1302 of kind
"Rebuild", 110 of kind "Task").

Mechanism (pacman side, "soname dependencies"). makepkg's libprovides
and libdepends: a `provides=('libfoo.so')` entry is expanded at build
time to `libfoo.so=1-64` from the built library's DT_SONAME; a
`depends=('libfoo.so')` entry is expanded to the soname version the
built binary's NEEDED asks for (PKGBUILD(5): "If the dependency name
appears to be a library, e.g. depends=(libfoobar.so), makepkg will try
to find a binary that depends on the library in the built package and
append the soname version needed by the binary"). The package
guidelines ask packagers to "List all external shared libraries of a
package in provides". Coverage today, measured on this host's sync db
(core+extra, 2026-09-17): 15251 packages, 1082 (7.1%) declare at least
one `libfoo.so=N-64` dependency (4065 sodeps in total), 741 packages
provide a soname, 19 still carry an unversioned `.so` dep (there is an
open 2026 todo "Review and clean up packages with unversioned sodeps",
36 packages). So pacman-level soname deps are opt-in and partial; the
authoritative rebuild trigger is the packager workflow above, whose
links database is complete.

Policy for users. ArchWiki System_maintenance, "Partial upgrades are
unsupported": "when new library versions are pushed to the
repositories, the Developers and Package Maintainers rebuild all the
packages in the repositories that need to be rebuilt against the
libraries ... upgrading only one package might also upgrade the
library (as a dependency), which might then break the other package
which depends on an older version of the library. That is why partial
upgrades are not supported." Anything not in the repos (AUR, locally
built, prebuilt) is the user's problem; there is no preserved-libs.

Same-soname handling. There is none automatic. When a maintainer
notices, they open an ordinary rebuild todo. Examples with their
verbatim descriptions:

- 2024-02-16 "Qt 6.6.2 ABI break", Antonio Rojas, 11 packages: "Qt
  6.6.2 broke its ABI compatibility promise in 6.6.2 and removed the
  _Zls6QDebugRK11QDockWidget symbol. This was temporarily reverted in
  6.6.2-2 to fix the breakage and has been restored in 6.6.2-4. These
  packages use this symbol and need to be rebuilt."
  https://archlinux.org/todo/qt-662-abi-break/
- 2025-01-12 "fmt 11.1.2 partial rebuild", Carl Smedstad: "fmt 11.1.0
  broke the ABI compatibility with earlier 11.x versions. It was
  believed to be fixed by 11.1.1, but there still was breakage".
  (soname libfmt.so.11 throughout 11.x)
  https://archlinux.org/todo/fmt-1112-partial-rebuild/
- 2025-05-06 "libconfig 1.8 rebuild", Robin Candau, 11 packages:
  "libconfig have introduced a silent ABI breakage in v1.8." The
  package repo then gained a README: "New libconfig release *may*
  include *silent* ABI breakage ... rebuild linked reverse
  dependencies to avoid any unexpected breaks."
  https://archlinux.org/todo/libconfig-18-rebuild/
- 2024-12-27 "rofi plugins rebuild against rofi 1.7.6": "Rofi 1.7.6
  came with a silent ABI bump that requires plugins to be rebuilt"
  (a plugin ABI number, not an ELF soname).
- OpenSSL 3.1 / 3.2 / 3.3 / 3.5.x todos (5-6 packages each, 2023 to
  2025) are precautionary, not ABI breaks: "OpenSSL 3.2 should be
  binary compatible with 3.0 and 3.1: However some packages might
  check the exact version during runtime and refuse to work."
  (OpenSSH's own check, openbsd-compat/openssl-compat.c, requires only
  the major to match for OpenSSL >= 3.0.)

Note that "Libgit2 ABI breakage for 1.9.0" (2024-12-30, 56 packages)
is not a same-soname case: libgit2 sets SOVERSION to MAJOR.MINOR
(src/libgit2/CMakeLists.txt) so 1.8 -> 1.9 moved libgit2.so.1.8 to
libgit2.so.1.9, and Arch provides `libgit2.so=1.9-64`. spdlog now does
the same (`libspdlog.so=1.17-64`) after its 2022 incident (see Debian).

### Debian

Policy text (Debian Policy 8.1, "Run-time shared libraries"): "Every
time the shared library ABI changes in a way that could break binaries
linked against older versions of the shared library, the SONAME of the
library and the corresponding name for the binary package containing
the runtime shared library should change." It adds that the SONAME
"need not, and indeed normally should not, change if new interfaces are
added but none are removed or changed".

Two dependency mechanisms (Policy 8.6):

- shlibs: one line per soname mapping it to a minimum package version;
  "A shlibs file only documents the last time the library ABI changed
  in any way and only provides information about the library as a
  whole, not individual symbols."
- symbols files (dpkg-gensymbols / dpkg-shlibdeps): per exported
  symbol, "the minimal version of the package any binary using this
  symbol will need". dpkg-gensymbols "Fails if some symbols have
  disappeared" at check level >= 1, so a removed symbol breaks the
  library's own build. The manpage is explicit about the limit: struct
  or semantic changes that leave the symbol table unchanged are not
  detected; the maintainer has to bump versions by hand from the
  upstream changelog.

Transitions. When a soname changes (or an ABI change is discovered),
the maintainer files a bug against release.debian.org usertagged
`transition`; the release team runs a "ben" tracker
(https://release.debian.org/transitions/), schedules binNMUs of the
reverse dependencies, and the set migrates to testing together.
wiki.debian.org/Teams/ReleaseTeam/Transitions defines a transition as
"the upload of a package requires changes (rebuilds or actual patches)
to reverse dependencies".

Same-soname handling. Debian's convention, stated in bug #1041302
(svt-av1): "renaming the binary package by attaching a Debian specific
suffix ... without changing the SONAME", with "proper Breaks+Replaces",
so that the rename forces a transition even though the file on disk
keeps its name. The `v5` suffix used for the 2015 libstdc++ C++11 ABI
change is the biggest instance (wiki.debian.org/GCC5: "libstdc++
(>= 5.1.1-20) doesn't change the soname, provides a dual ABI",
"rename the package, append 'v5' to the name of the package (e.g.
libfoo2 -> libfoo2v5)"); UDD shows 302 mass-filed bugs titled "library
transition may be needed when GCC 5 is the default" between 2015-07-03
and 2015-08-09. Qt is handled by a per-patch-release virtual package:
every consumer depends on `qtbase-abi-5-15-4` / `qtbase-abi-6.11.2`,
and the number in the name is bumped when upstream breaks ABI in a
point release (bug #917400; a `qtbase-abi-6.11.2` transition is on the
tracker today).

### Fedora / RHEL

Automatic deps: RPM's `elfdeps` generator encodes, for every package,
"DT_SONAME, DT_NEEDED fields of the SHT_DYNAMIC section, and symbol
versioning information in the SHT_GNU_verdef and SHT_GNU_verneed
sections" (elfdeps(1)). So every RPM carries `Requires:
libc.so.6(GLIBC_2.34)(64bit)` style dependencies with 100% coverage,
not the 7% opt-in of pacman. Still soname plus version-node
granularity, not per-symbol, not layout.

Mass rebuild: every release, in Rawhide before the change deadline,
"The goal is to rebuild every single Fedora package"; the Fedora 45
mass rebuild (2026-07-15 to 2026-08-11) was driven by "GNU Toolchain
Update (gcc 16.2, binutils 2.47, glibc 2.44, gdb 17.2)", Golang, Perl,
Lua, Python 3.15, Protobuf 5.x/6.x. Rationale is toolchain flags and
new runtimes, not a belief that sonames lie.

ABI checking: Fedora built libabigail (abidiff, abipkgdiff,
fedabipkgdiff) specifically to catch same-soname breaks. Taskotron's
`dist.abicheck` ran abipkgdiff on every Koji build of a critpath
package against the previous build and reported PASSED / FAILED /
NEEDS_INSPECTION; Taskotron was sunset around 2020. Its successor,
rpminspect in Fedora CI, has an `abidiff` inspection using libabigail
on each build comparison. RHEL's Application Compatibility Guide then
sorts packages into compatibility levels: level 1 "stable within the
lifetime of a major release and ... across the next two major
releases", level 2 (the default) stable within one major release,
levels 3 and 4 with no cross-version guarantee.

### openSUSE

Factory runs an `abichecker` review bot (openSUSE-release-tools/
abichecker/abichecker.py) on submit requests: it pairs old and new
libraries by soname regex, runs `abi-dumper` and
`abi-compliance-checker`, and comments on the request; the code's own
comment says "default is to accept the review, just leave a note if
there were problems". Non-blocking.

### Gentoo

Two mechanisms. `FEATURES=preserve-libs` (Portage 2.2+) "preserves
libraries when sonames change during upgrade or downgrade, only as
necessary to satisfy shared library dependencies of installed
consumers": the old `libfoo.so.1` stays on disk, consumers keep
working, `emerge @preserved-rebuild` rebuilds them, and the preserved
file is removed when no consumer references it. It made `revdep-
rebuild` mostly obsolete. Second, sub-slots with the `:=` slot operator
(EAPI 5): the ebuild author sets `SLOT="0/14"` and any consumer that
depended on `libpng:0=` is rebuilt when the sub-slot changes. The
devmanual is the one place a distro writes the blind spot down:

    When made aware of ABI breakage, change the subslot. Note that the
    subslot does not have to strictly be the SONAME and therefore could
    be an arbitrary string ... Be aware that some upstreams may make
    releases without verifying if binary compatibility has been broken
    in a minor release. You should check using tools like
    dev-util/libabigail or ABI Laboratory.

So Gentoo's answer to a same-soname break is a human bumping a string
that is not the soname; the machinery is keyed on that string.

## 2. Frequency numbers

### Arch: rebuild todo lists per year

Parsed from all 29 pages of https://archlinux.org/todo/ on 2026-09-17
(1412 lists). "Library bump" is a rebuild list that is not a language
runtime rebuild (python/perl/ruby/php/lua/erlang/nodejs/LLVM/...) nor
housekeeping (re-sign, .SRCINFO, reproducibility, LTO, cleanup).

    year  todo lists  kind=Rebuild  library bumps  pkgs in lib rebuilds
    2021          96            82            n/a                   369
    2022         130           111            n/a                   459
    2023         115            99             82                  1291
    2024         175           153            115                  2321
    2025         200           175            155                  3805
    2026*        128           119            n/a                  2377
    (* to 2026-09-17)

Explicit same-soname rebuilds among those: 0 in 2023, 3 in 2024 (Qt
6.6.2, rofi plugin ABI, plus the precautionary OpenSSL 3.3), 2 in 2025
(fmt 11.1.0, libconfig 1.8) plus three precautionary OpenSSL 3.5.x
lists. Descriptions that spell out `old.so -> new.so` exist for 8, 10
and 5 lists in 2023, 2024, 2025 (most descriptions are just "packages
go to staging", so the count of soname-bump lists is the library-bump
column, not this one). Serial bumpers dominate the list: Poppler (a new
libpoppler.so.NNN almost monthly, 12 packages each), mupdf (every
release, statically linked consumers), protobuf/abseil/grpc (every
release), boost (every release, ~90 packages), icu (twice a year, ~145
packages), LLVM (twice a year, ~55 packages), fmt, spdlog, libjxl,
opencv, gdal, hdf5.

### Debian: transitions per year

From the public UDD mirror (udd-mirror.debian.net), bugs usertagged
`transition` by release.debian.org@packages.debian.org, by arrival
year, open plus archived:

    2011  72   2015 219   2019  86   2023 125
    2012  64   2016 148   2020 149   2024 156
    2013  97   2017  99   2021  91   2025 142
    2014 105   2018 144   2022 188   2026 142 (to Sep 17)

The tracker page today lists 54 planned, 14 ongoing, 3 almost finished
and 3 permanent (haskell, ocaml, rust).

### Debian: same-soname breaks, from bug titles (UDD)

Bugs whose title contains "ABI" and "soname/sover" and a negation
("without", "no", "not", "same", ...): 56 bugs from 2003 to 2026.

    2003-2005 10   2011  5   2017  5   2021  1   2025  1
    2008-2010  6   2012  2   2018  4   2022  4   2026  2
                   2014  1   2019  1   2023  6
                   2015  1   2020  4   2024  2
                   2016  2

Broader net, titles matching "ABI" plus break/broke/incompat/change
(any severity; RC = serious/grave/critical; "soname" = title also
mentions soname/sover):

    year  all  RC  soname     year  all  RC  soname
    2015   16  15   0         2021   14  12   1
    2016    3   3   2         2022   16  13   4
    2017   23  16   5         2023   11  11   6
    2018   18  15   4         2024   19  14   3
    2019    7   6   1         2025    7   6   1
    2020   17  10   2         2026   10   9   2

Symbols-file detections (titles matching "dpkg-gensymbols" or "symbols
... disappeared/removed/missing", overwhelmingly "FTBFS: dpkg-gensymbols:
error: some symbols or patterns disappeared in the symbols file"): 61,
35, 25, 25, 16, 50, 32, 26, 30, 42, 21, 17 for 2015 through 2026. This
is the build-time catch rate of the per-symbol mechanism; many are C++
template instantiation churn from a new GCC rather than upstream
removals, so it is an upper bound on "symbol removed under the same
soname" and says nothing about layout changes.

Reading the three tables together: Debian handles ~140 transitions a
year and finds ~3 to 6 same-soname breaks a year serious enough to be
titled that way, i.e. 2-4% of ABI events, archive-wide (>30k source
packages).

### Fedora / libabigail / openSUSE

No published aggregate statistics were found. The Red Hat Developer
post "ABI change analysis of Fedora packages" (2017-02-28) is a tool
walkthrough (spice-server F23 vs F25: 3 added functions, 85 harmless
type changes, 0 incompatible). Taskotron's result archive is gone with
the service. openSUSE's abichecker stores results in a database that
is not published. Marked unverified below.

### ABI Tracker (abi-laboratory.pro), per library

The tracker lists 797 C/C++ libraries. Its per-version "backward
compat." percentage comes from abi-compliance-checker: anything below
100% means some public type or symbol reachable from the headers
changed, which is a superset of "an already-built binary would break".
The tracker stopped updating most timelines between 2020 and 2023, so
this covers roughly 2010 to early 2023. Parsed from the timeline pages
on 2026-09-17, restricted to libraries our prebuilt binaries NEED:

    library     versions  span            soname  flagged  same-soname  last upd
                compared                  changes versions flagged
    glibc         25      2011..2023.02     0       18        18        2023-03
    zlib          26      1996..2017.01     1        9         9        2020-09
    gmp           26      2001..2020.11     1        2         2        2020-12
    mpfr          18      2009..2023.01     2        4         2        2023-03
    ncurses       12      2002..2022.12     1        6         5        2023-03
    libffi        20      2008..2019.11     2        0         0        2022-02
    openssl      113      2010..2021.12     3       27        24        2021-12
    libxml2       28      2008..2022.10     0        7         7        2023-03
    util-linux    48      2011..2023.03     0        6         6        2023-03
    xz            16      2010..2018.04     0        0         0        2020-09
    zstd          38      2016..2023.02    14       17         6        2023-03
    readline       8      2006..2020.09     3        1         0        2020-09
    pcre2         12      2015..2020.05     2        1         0        2020-09
    total        377                       29       98        79 (21 with
                                                                removed syms)

So by the tracker's strict metric, 79 of 377 version steps (21%) of
these libraries changed something under an unchanged soname, and 21
removed at least one symbol. Caveats that matter for our question:

- glibc's "removed" symbols (2.32: 18, 2.31: 9, 2.26: 5) are removals
  from the default version node; the compat-versioned symbols stay,
  which is why glibc has kept libc.so.6 since 1997 and PEP 600 records
  that "glibc maintainers ... advised us that we should assume that
  glibc will maintain backwards-compatibility indefinitely". Old
  binaries keep running; only new links are refused.
- Most sub-100% entries are 99.x%: one field in a rarely used struct.
  The big drops are the notorious ones: zlib 1.2.4 (85.9%, 12 removed,
  2010), zlib 1.2.9 (73.1% in the zlib-ng-compat timeline, 2017),
  util-linux 2.26 (68.5%, 227 removed, 2015; libmount/libblkid
  internals), ncurses 5.5 (67.1%, 2005), mpfr 4.1.1 (69.9%, 2022;
  likely a build-config artefact, unverified), libxml2 2.10.0 (93.8%,
  103 removed, 2022; deprecated symbols dropped, later restored in
  2.10.x/2.11 under pressure).
- openssl's 24 same-soname flags are all inside the 0.9.8, 1.0.0, 1.0.x
  and 1.1.x letter releases; OpenSSL's stated policy since 1.1.0 is
  ABI stability within a soname and the numbers (99.9x%) are opaque
  struct growth.

## 3. Incidents: same-soname breaks that hit real users

Date | distro | library | what | link

- 2022-08 | Arch first, then Fedora, Slackware, all rolling | glibc
  2.36 (libc.so.6) | Dropped `--hash-style=both`, so the DT_HASH table
  vanished; Easy Anti-Cheat and libstrangle read it and games failed
  to launch. Arch shipped 2.36-2 restoring both; Fedora 2.36-7.fc37
  (2022-10-19) set `LDFLAGS.so`/`LDFLAGS-rtld` to `--hash-style=both`.
  https://bugzilla.redhat.com/show_bug.cgi?id=2129358 ,
  https://www.phoronix.com/news/Glibc-2.36-EAC-Problems
- 2025-02 | Arch, then everyone shipping 2.41 | glibc 2.41 (libc.so.6)
  | dlopen no longer makes the stack executable for objects that
  request it ("cannot enable executable stack as shared object
  requires: Invalid argument"); Steam games with FMOD, Discord, Julia,
  MATLAB, Mono broke. Upstream workaround via the glibc.rtld.execstack
  tunable merged 2025-04-08.
  https://gitlab.archlinux.org/archlinux/packaging/packages/glibc/-/issues/19 ,
  https://www.phoronix.com/news/Glibc-WA-Steam-Exec-Stack
- 2022-04 | Arch (FS#74371), any distro on 1.2.12 | zlib 1.2.12
  (libz.so.1) | New CRC code became sensitive to garbage above bit 32
  in the crc argument; Java's CRC32 passes such values, so Spring Boot,
  NetBeans, JetBrains Toolbox, Minecraft/Fabric saw "invalid entry
  CRC". Upstream commit ec3df002 (2022-03-30): "The previous releases
  of zlib were not sensitive to incorrect CRC inputs with bits set
  above the low 32. This commit restores that behavior, so that
  applications with such bugs will continue to operate as before."
  Arch zlib 1.2.12-2, 2022-04-24. https://bugs.archlinux.org/task/74371
- 2022-04 | self-built zlib (HPC module trees, vendor toolchains) |
  zlib 1.2.12 (libz.so.1) | configure bug dropped the `ZLIB_1.2.x`
  version nodes from some builds; consumers failed with "version
  `ZLIB_1.2.2' not found (required by libtcl8.6.so)". Fix incorporated
  upstream 2022-10-06 (1.2.13). Distro packages unaffected.
  https://github.com/madler/zlib/issues/642
- 2024-02 | Arch | Qt 6.6.2 (libQt6Widgets.so.6) | patch release
  removed `operator<<(QDebug, const QDockWidget&)`; dolphin-emu,
  obs-studio, shotcut, texstudio and 7 more rebuilt.
  https://archlinux.org/todo/qt-662-abi-break/
- 2024-12 / 2025-01 | Arch | fmt 11.1.0 (libfmt.so.11) | ABI broke
  within the 11.x soname; easyeffects, surge-xt rebuilt after 11.1.2.
  https://archlinux.org/todo/fmt-1112-partial-rebuild/
- 2025-05 | Arch | libconfig 1.8 (libconfig.so.11 at the time) |
  "silent ABI breakage"; picom, sslh, libguestfs, toxcore and 7 more
  rebuilt. https://archlinux.org/todo/libconfig-18-rebuild/
- 2022-07 | Debian (#1015742, #1016408, #1016466) | spdlog 1.10
  (libspdlog.so.1) | rotating_file_sink constructor symbol gone; nheko
  failed to start with a symbol lookup error. Debian renamed the
  package to libspdlog1.10 (NMU 2022-08-15); upstream moved to
  MAJOR.MINOR SOVERSION from 1.11. https://bugs.debian.org/1015742
- 2023-07 | Debian (#1041302) | svt-av1 1.6.0 (libSvtAv1Enc.so.1) |
  field removed from the middle of EbSvtAv1EncConfiguration; handled by
  the "Debian specific suffix" package rename.
  https://bugs.debian.org/1041302
- 2018-07 | Debian (#902941) | libuv 1.21.0 (libuv.so.1) | `uv_fs_type`
  enum value inserted mid-enum; lua-luv "UNKNOWN FS TYPE 29", nodejs
  affected; Debian patched the enum order in 1.21.0-2.
  https://bugs.debian.org/902941
- 2018-06 | Debian (#902281) | gsl 2.5 (libgsl.so.23) | nine
  deprecated functions removed; libmath-gsl-perl "undefined symbol:
  gsl_linalg_hessenberg"; Debian re-enabled the deprecated symbols by
  patch rather than bump. https://bugs.debian.org/902281
- 2025-01 | Debian (#1092247) grave; Arch rebuilt 2025-03 |
  gumbo-parser 0.12.3 (libgumbo.so.2) | upstream commit changed
  parser output structure; HTML::Gumbo lost the `<html>` element.
  Fixed in 0.13.0 with a real soname bump (libgumbo.so.3).
  https://bugs.debian.org/1092247
- 2020-2024 | Debian, RC severity | grpc (#963583), liburing (#972758),
  libcamera (#962650), tinyxml2 (#898535), leveldb (#877773), confuse
  (#872115), tpm2-tss (#1051284), opendht (#1051972), benchmark
  (#1059785), neatvnc 0.8.0 (#1065824), dynarmic, libvcflib: all
  "breaks ABI without SONAME bump" bugs from the UDD query above.
- 2015-07 | Debian, Arch, Fedora, everyone | libstdc++ (GCC 5, still
  libstdc++.so.6) | new std::string/std::list under `std::__cxx11`;
  old binaries kept running (dual ABI, symbols retained) but any
  C++ library built with the new ABI is incompatible with consumers
  built with the old one under the same soname: Debian renamed ~300
  library packages with `v5`, Arch rebuilt the C++ world.
  https://wiki.debian.org/GCC5

Pattern worth noting: of the incidents on libraries our binaries need
(glibc, zlib, libstdc++), none removed or re-laid-out a symbol an old
binary calls; each was a behavioural change (ELF hashing, stack
permission, input strictness, dual ABI) that broke a binary relying on
something outside the documented ABI. A soname check, a symbol-version
check and a libabigail diff would all have passed them.

## 4. What each mechanism actually checks

By soname (file present, name matches):
- pacman sodeps `libfoo.so=1-64` (opt-in, 7% of Arch packages).
- Debian `shlibs` (soname to minimum package version; the minimum
  version is bumped by hand).
- Gentoo `preserve-libs` (keys purely on the soname on disk).
- `ldd` / a "every NEEDED resolves" check. This is our proposal. It is
  exactly pacman's sodeps without the version-bump-by-hand escape
  hatch that Debian shlibs and Gentoo sub-slots add.

By soname plus symbol version nodes:
- RPM `elfdeps` (`libc.so.6(GLIBC_2.34)(64bit)`), automatic, complete.
- glibc/libstdc++/zlib symbol versioning itself; manylinux
  (PEP 513/600) pins on `GLIBC_2.x`/`GLIBCXX_3.4.x` nodes and lets
  `auditwheel` reject any wheel that needs newer ones. Direction
  matters: version nodes protect a *newer* binary from an *older*
  library ("version GLIBC_2.34 not found"). For our case (binary older
  than the library) they add nothing over the soname unless a node is
  removed, which glibc never does and zlib did once by accident.

By symbol (name present):
- Debian `symbols` files: per-symbol minimum version; build fails when
  a symbol disappears; installed dependencies are `>=` the version that
  introduced each symbol the binary imports. Catches removals (Qt
  6.6.2, gsl 2.5, spdlog 1.10, gconf, libdap in the Debian list) but
  not enum/struct changes (libuv 1.21, svt-av1 1.6, gumbo, fmt).
  Equivalent for us: `nm -D --undefined-only binary` versus
  `nm -D --defined-only lib` per NEEDED, i.e. an `ld.so` bind-now dry
  run (`LD_BIND_NOW=1` with `LD_WARN`/`--verify`-style checks).

By full ABI diff (types, layouts, enums, vtables):
- libabigail `abidiff`/`abipkgdiff` (Fedora CI rpminspect, formerly
  Taskotron; needs debug info for both versions).
- abi-compliance-checker / abi-dumper (openSUSE abichecker review bot,
  ABI Tracker). Needs headers or DWARF; only practical when both old
  and new library builds with debug info are at hand, which a consumer
  of a distro library is not.

By nothing at all, just a fence:
- Fedora mass rebuild, RHEL compat levels, Steam Runtime containers,
  manylinux and AppImage "bundle everything except this list". The
  lists agree: manylinux1 allows 21 host sonames (glibc family,
  libgcc_s.so.1, libstdc++.so.6, libX11/ICE/SM/GL, glib); the AppImage
  excludelist is glibc, libstdc++/libgcc_s, Mesa/X11/Wayland, ALSA,
  fontconfig/freetype/harfbuzz, and explicitly libz.so.1, libexpat.so.1,
  libuuid.so.1, libgmp.so.10 ("Coreutils depends on it; safe to assume
  on target systems"). Valve's steam-runtime goals.md gives the reason
  the fence exists: "Games can break when libraries claim to be
  compatible (by having the same ELF DT_SONAME) but are in fact not
  compatible", citing libcurl.so.4 built against different OpenSSL
  majors and libraries whose symbol set depends on build options.

Where "sonames resolve" sits: it is the weakest tier, identical to what
Arch's own package manager checks for the 7% of packages that opt in,
and to what Gentoo's preserve-libs uses to decide what to keep. Every
distro pairs that tier with a human-driven override (Debian package
rename, Gentoo sub-slot bump, Arch rebuild todo) for the 2-4% of ABI
events that are same-soname breaks, and the incidents on core
libraries were behavioural, invisible to any of the tiers. A
per-symbol check (defined-symbol set of each NEEDED library covers the
binary's undefined set) is cheap, has no false positives, and would
have caught every "removed symbol" incident above; it would not catch
the layout/enum class, which on the core-library set has not occurred
in the record examined.

## 5. Sources

Arch
- https://archlinux.org/todo/ (all 29 pages fetched 2026-09-17; parsed
  counts above)
- https://archlinux.org/todo/qt-662-abi-break/
- https://archlinux.org/todo/fmt-1112-partial-rebuild/
- https://archlinux.org/todo/libconfig-18-rebuild/
- https://archlinux.org/todo/libgit2-abi-breakage-for-190/
- https://archlinux.org/todo/openssl-32/
- https://archlinux.org/todo/libxml2-2142-rebuild/ (libxml2.so.2 ->
  libxml2.so.16, 371 packages, 2025-04-22)
- https://archlinux.org/todo/libtheora-120-rebuild/
- https://wiki.archlinux.org/title/DeveloperWiki:How_to_be_a_packager
- https://wiki.archlinux.org/title/System_maintenance (Partial
  upgrades are unsupported)
- https://wiki.archlinux.org/title/PKGBUILD (depends / provides .so)
- https://wiki.archlinux.org/title/Arch_package_guidelines
- https://man.archlinux.org/man/extra/devtools/sogrep.1.en ,
  checkpkg.1, find-libprovides.1, devtools.7
- https://www.mail-archive.com/arch-commits@lists.archlinux.org/msg674403.html
  (libconfig README commit, 2025-05-06)
- https://bugs.archlinux.org/task/74371 (zlib 1.2.12 CRC)
- https://gitlab.archlinux.org/archlinux/packaging/packages/glibc/-/issues/19
- local: `pacman -Si` over core+extra on this host, 2026-09-17;
  /etc/os-release and /usr/lib/os-release
- https://github.com/libgit2/libgit2/blob/main/src/libgit2/CMakeLists.txt
  (SOVERSION MAJOR.MINOR)
- openssh-portable, openbsd-compat/openssl-compat.c (ssh_compatible_openssl)
  https://github.com/openssh/openssh-portable

Debian
- https://www.debian.org/doc/debian-policy/ch-sharedlibs.html
- https://manpages.debian.org/testing/dpkg-dev/dpkg-gensymbols.1.en.html
- https://wiki.debian.org/Teams/ReleaseTeam/Transitions
- https://release.debian.org/transitions/ and
  https://release.debian.org/transitions/export/packages.yaml
- UDD public mirror: psql -h udd-mirror.debian.net -U udd-mirror udd
  (tables bugs, archived_bugs, bugs_usertags), queried 2026-09-17
- https://bugs.debian.org/1015742 (spdlog), /1041302 (svt-av1),
  /902281 (gsl), /902941 (libuv), /1092247 (gumbo), /917400 (qtbase-abi)
- https://wiki.debian.org/GCC5 ;
  https://lists.debian.org/debian-devel-announce/2015/08/msg00002.html

Fedora / RHEL / RPM
- https://fedoraproject.org/wiki/Fedora_45_Mass_Rebuild
- https://fedoraproject.org/wiki/Taskotron/Tasks/dist.abicheck
- Red Hat Developer, "ABI change analysis of Fedora packages", 2017-02-28
  https://developers.redhat.com/blog/2017/02/28/
- Red Hat Developer, "abipkgdiff: Ensuring ABI compliance for shared ELF
  library packages", 2016-02-04 https://developers.redhat.com/blog/2016/02/04/
- https://sourceware.org/libabigail/manual/abipkgdiff.html
- https://rpminspect.readthedocs.io/en/latest/usage.html (abidiff)
- rpm docs/man/elfdeps.1.scd https://github.com/rpm-software-management/rpm
- https://access.redhat.com/articles/rhel9-abi-compatibility
- https://bugzilla.redhat.com/show_bug.cgi?id=2129358

openSUSE / Gentoo
- openSUSE-release-tools, abichecker/abichecker.py
  https://github.com/openSUSE/openSUSE-release-tools
- https://wiki.gentoo.org/wiki/Preserve-libs
- https://devmanual.gentoo.org/general-concepts/slotting/index.html
- https://wiki.gentoo.org/wiki/Sub-slots_and_Slot-Operators

Cross-distro
- https://abi-laboratory.pro/tracker/index.html (797 libraries) and
  https://abi-laboratory.pro/tracker/timeline/<lib>/ for glibc, zlib,
  zlib-ng-compat, gmp, mpfr, ncurses, libffi, openssl, libxml2,
  util-linux, xz, zstd, readline, pcre2, python, curl, binutils
- https://github.com/lvc/abi-tracker ,
  https://github.com/lvc/abi-compliance-checker
- https://peps.python.org/pep-0513/ (manylinux1 soname whitelist) ,
  https://peps.python.org/pep-0600/ (perennial manylinux rationale)
- https://github.com/AppImageCommunity/pkg2appimage/blob/master/excludelist
- https://github.com/ValveSoftware/steam-runtime/blob/master/doc/goals.md
  ("Avoiding incompatibilities between libraries")
- https://lwn.net/Articles/658809/ (Debian dropping the LSB, 2015)
- https://gcc.gnu.org/onlinedocs/libstdc++/manual/abi.html ,
  https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_dual_abi.html
- https://github.com/madler/zlib/issues/642 and commit ec3df00224d4
- https://www.phoronix.com/news/Glibc-2.36-EAC-Problems ,
  https://www.phoronix.com/news/Glibc-WA-Steam-Exec-Stack

## 6. Unverified items

- Fedora/libabigail aggregate detection counts: no published numbers
  found (Taskotron archive gone; the 2017 Red Hat post is a single-case
  demo). Dodji Seketeli's conference talks were not fetched; if they
  contain statistics they are not reflected here.
- openSUSE abichecker: mechanism read from source; its result database
  and hit rate are not public.
- Ponomarenko and Rubanov, "Backward compatibility of software
  interfaces: steps towards automatic verification" (Programming and
  Computer Software, 2012) reportedly contains archive-wide ABI
  statistics; the paper is paywalled and was not read.
- ABI Tracker percentages are as published; the tool's classification
  of "harmful" was not re-verified per report. mpfr 4.1.1 at 69.9% is
  suspected to be a build/config artefact of the tracker rather than a
  real break (4.1.1 was a bugfix release).
- Arch libconfig issue #2 (which symbol/struct changed in 1.8) sits
  behind Anubis on gitlab.archlinux.org and was not read; the todo
  description and the package README commit are quoted instead. Same
  for the glibc 2.41 Arch issue #19 (Phoronix/GamingOnLinux coverage
  used).
- Steam Runtime distro-assumptions.md and container-runtime.md on
  gitlab.steamos.cloud were blocked (Anubis); goals.md from the GitHub
  mirror is quoted instead.
- Arch "library bump" classification is a regex over todo titles
  (runtime and housekeeping lists excluded); a handful of lists could
  be misfiled either way. The explicit same-soname list was read from
  the todo descriptions and is exact for 2023-2025.
- Debian per-year bug counts depend on title wording; bugs about
  same-soname breaks that were titled differently ("symbol lookup
  error", "undefined symbol") are not included, so the 56 is a floor.
