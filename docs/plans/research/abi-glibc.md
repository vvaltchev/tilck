# glibc ABI stability without soname changes: incident survey

Research note for the "prebuilt host binaries survive a distro update?"
question. Scope: glibc only (libc, libm, libdl, libpthread, librt,
libcrypt, ld.so), releases 2.13 (2011) through 2.44 (2026-07).

Method: glibc NEWS file (sourceware git, HEAD, includes 2.44 and the
2.45 stub), the per-release "Deprecated and removed features, and other
changes affecting compatibility" sections, the x86_64 `libc.abilist`
and `libm.abilist` files, sourceware bugzilla, glibc git commits, distro
bug trackers, upstream project issues and LWN. Everything dated below
was checked against one of those; anything I could not confirm is
flagged "unverified" and listed again in section 6.

Class key used throughout:

- (a) forward-incompatible: binary built against a NEWER glibc run on
  an OLDER one. Detected at load time by symbol-version resolution
  ("version `GLIBC_2.34' not found").
- (b) backward-incompatible: binary built against an OLDER glibc run on
  a NEWER one and it fails. The case a soname check cannot see.
- (c) behaviour change with no ABI change: same symbols resolve, the
  code does something different.

## 1. glibc's stated compatibility policy

The promise is one-directional, and it is largely unwritten. The best
public statements:

- glibc wiki FAQ, "What is symbol versioning good for?":
  "One version of an interface might have been introduced in a previous
  version of the GNU C library but the interface or the semantics of the
  function has been changed in the meantime. For binary compatibility
  with the old library, a newer library needs to still have the old
  interface for old programs. On the other hand, new programs should
  use the new interface. Symbol versioning is the solution for this
  problem." [S1]
- DJ Delorie (glibc maintainer), Red Hat Developer, 2019-08-01: "One of
  the GNU C Library's (glibc's) unwritten rules is that a program built
  against an old version of glibc will continue to work against newer
  versions of glibc." The same article states that the converse does
  not hold (you cannot run a new program on an old glibc) and that
  linking one process against two DSOs that use different versions of
  the same symbol, or using `dlsym` to pick a version, "is not
  supported". [S2]
- Florian Weimer, on the 2.34 library merge: "These changes have been
  implemented in a backward-compatible fashion, so even though
  libpthread is gone as a separate object, all the public functions it
  used to provide (such as pthread_create) are still available." [S3]
- Release process wiki: releases "every 6 months around 1st February and
  1st August, subject to any regressions or ABI issues"; release
  branches receive "conservative bug fixes ... while retaining backwards
  compatibility"; anything "changing the public ABI and so requiring
  check-abi updates" is out of bounds after the slush. [S4]

Mechanism. The sonames have been fixed since glibc 2.0 (libc.so.6, 1997)
and every exported symbol carries a version node (GLIBC_2.2.5, ...,
GLIBC_2.44 on x86_64). Old versions are never removed for an existing
ABI; a changed interface gets a new node and the old node keeps the old
semantics as a "compat symbol" (memcpy@GLIBC_2.2.5 vs memcpy@@GLIBC_2.14
is the canonical example). The check is enforced in-tree by the
`*.abilist` files (`make check-abi`).

What the promise does NOT cover, from the same sources and from how the
maintainers closed the bugs below:

- GLIBC_PRIVATE symbols. Internal, renamed or dropped at will; mixing
  pieces of two glibc builds is unsupported [S2, S24].
- Statically linked programs (locale data format changed in 2.27 for
  static binaries; static dlopen deprecated since 2.27) [S5, S1].
- Programs that rely on undefined behaviour: memcpy overlap (closed
  NOTABUG in 2010), double free, non-transitive qsort comparators
  (Qualys 2024: no CVE, "quality of implementation"), hand-rolled
  termios2 ioctl hacks (2025: "the universe is full of hacks now").
- Programs that parse libc.so.6 as an ELF file (EAC and DT_HASH), or
  that overlay glibc-owned kernel structures (tcmalloc and struct rseq).
- Anything only documented in NEWS under "changes affecting
  compatibility": that section exists in every release since 2.26 and
  is the de facto changelog of class (b)/(c) changes.

Symbol-version churn per release (x86_64, number of exported symbols in
each version node, from HEAD `libc.abilist` / `libm.abilist`) [S6]:

```
libc: 2.13:5  2.14:7  2.15:8  2.16:10  2.17:6  2.18:3  2.22:1  2.23:5
      2.24:1  2.25:7  2.26:13 2.27:30  2.28:28 2.29:3  2.30:9  2.31:1
      2.32:11 2.33:12 2.34:212 2.35:4  2.36:17 2.38:40 2.39:75 2.41:2
      2.42:17 2.43:8   (2.37, 2.40, 2.44: none)
libm: 2.15:81 2.18:3  2.23:4  2.24:6  2.25:50 2.26:136 2.27:421 2.28:52
      2.29:5  2.31:16 2.32:1  2.35:92 2.38:2  2.39:1  2.40:40  2.41:56
      2.42:40 2.43:52 2.44:2
```

Every one of those nodes is a class (a) trigger: a binary built against
that release and touching one of those symbols will not load on any
older glibc. The big ones in practice: 2.14 (memcpy), 2.15 (`__*_finite`
with -ffast-math), 2.17 (clock_gettime in libc), 2.25 (getrandom),
2.27 (glob, memfd_create), 2.28 (fcntl64, statx), 2.29 (exp/log/pow),
2.32 (`__libc_single_threaded`, used by libstdc++ from GCC 11), 2.33
(stat family), 2.34 (`__libc_start_main`: every binary), 2.36
(arc4random), 2.38 (`__isoc23_strtol` etc., pulled in by GCC 15's
default -std=gnu23), 2.42 (cfsetospeed and friends), 2.43 (acosf,
coshf, ... in libm).

## 2. Incident table

Overview (one line per incident; details with who/what/link follow).
"soname" = did the soname change; "symver" = did symbol-version
resolution detect the break before the program misbehaved.

```
Id   date     ver   cls  soname symver  change
I01  2011-02  2.13  c    no     no      x86_64 memcpy copies backwards
I02  2011-06  2.14  a    no     yes     memcpy@GLIBC_2.14; old = memmove
I03  2012-03  2.15  a    no     yes     __*_finite@GLIBC_2.15 (libm)
I04  2012-12  2.17  a    no     yes     clock_gettime moved into libc
I05  2016-08  2.24  b    no     no      libio vtable validation aborts
I06  2017-02  2.25  a/b  no     yes     malloc_get_state -> compat stub
I07  2017-08  2.26  c    no     no      spin_unlock weaker; NIS deprecated
I08  2018-02  2.27  c/a  no     no/yes  new float libm; static locales
I09  2018-08  2.28  c    no     no      stdio EOF becomes sticky
I10  2018-08  2.28  a    no     yes     fcntl64, statx, C11 threads
I11  2019-02  2.29  c/a  no     no/yes  tcache double-free abort; exp@2.29
I12  2019-08  2.30  c    no     no      copy_file_range no emulation
I13  2020-02  2.31  c    no     no      gettimeofday tz, dlopen ctor abort
I14  2020-08  2.32  c/a  no     no/yes  sysctl ENOSYS; __libc_single_thr.
I15  2021-02  2.33  c    no     no      faccessat2 + seccomp EPERM
I16  2021-02  2.33  a    no     yes     stat/fstat/lstat@GLIBC_2.33
I17  2021-08  2.34  a/c  no     yes     libpthread merge, __libc_start_main
I18  2021-08  2.34  b    no     late    GLIBC_PRIVATE __libc_dlsym etc gone
I19  2021-08  2.34  b/c  no     late/no malloc_set_state gone; hooks inert
I20  2021-08  2.34  c    no     no      clone3 + seccomp EPERM
I21  2022-02  2.35  c    no     no      rseq registration by glibc
I22  2022-02  2.35  b    no     no      r_debug r_version 2; catchsegv gone
I23  2022-08  2.36  b    no     no      libc.so.6 built without DT_HASH
I24  2022-08  2.36  c    no     no      dlsym(RTLD_NEXT) default version
I25  2022-08  2.36  a    no     yes     arc4random, fsmount, ...
I26  2023-02  2.37  c    no     no      legacy hwcaps dirs not searched
I27  2023-07  2.38  a/b  see    yes     libcrypt off by default; isoc23
I28  2024-01  2.39  b/c  see    n/a     libcrypt removed; qsort reverted
I29  2024-07  2.40  c    no     no      __rseq_size 32 -> 20
I30  2025-01  2.41  b    no     no      dlopen refuses exec-stack objects
I31  2025-01  2.41  c    no     no      abort vs longjmp; extensible rseq
I32  2025-07  2.42  c/a  no     no/yes  termios arbitrary speeds rework
I33  2025-07  2.42  c    no     no      MADV_GUARD_INSTALL stacks (tools)
I34  2026-01  2.43  a/b  no     yes     dumped heaps gone; new libm nodes
I35  2026-07  2.44  a    no     yes     cosh/sinh@GLIBC_2.44; nothing else
```

### I01  2.13 (2011-02-01): memcpy copies backwards on SSSE3 x86_64

- change: commit 6fb8cbcb58a2 "Improve 64bit memcpy/memmove for Atom,
  Core 2 and Core i7", shipped in 2.13; Fedora 14 carried it earlier as
  glibc 2.12.90-4 (Sept 2010).
- who broke: Adobe Flash Player 10.2 x86_64 (distorted audio), "old
  gstreamer plugins", squashfs-tools (per LWN); all called memcpy with
  overlapping buffers.
- soname: no. symver: no (memcpy@GLIBC_2.2.5 changed behaviour in place).
- maintainers: closed NOTABUG in Red Hat bug 638477 (Andreas Schwab:
  "crap software violating well known rules"); Linus Torvalds filed
  BZ 12518 on 2011-02-25 ("the behavior has changed, and in the process
  broken existing binaries").
- class: c.
- links: [S7] RH 638477, [S8] BZ 12518, [S9] LWN "Glibc change
  exposing bugs" (2010-11-10).

### I02  2.14 (2011-06-01): memcpy@GLIBC_2.14

- change: commit 0354e355014b "Work around old buggy program which
  cannot cope with memcpy semantics" (Drepper, 2011-04-01): new node
  memcpy@@GLIBC_2.14 on x86_64; memcpy@GLIBC_2.2.5 becomes an alias of
  memmove, so I01 is retroactively fixed for old binaries. Not in NEWS.
- who broke: every binary built on glibc >= 2.14 and run on RHEL 6 /
  CentOS 6 (2.12): "version `GLIBC_2.14' not found". The most searched
  glibc error of the decade.
- soname: no. symver: yes (that is the point).
- class: a.
- links: [S10] commit, [S6] abilist (GLIBC_2.14 memcpy).

### I03  2.15 (2012-03-21): `__*_finite@GLIBC_2.15`

- change: "Integrate libm with gcc's -ffinite-math-only option" (NEWS
  2.15); 81 new libm symbols such as __exp_finite.
- who broke: binaries compiled with -ffast-math / -Ofast on 2.15+ fail
  on older glibc. Which release stopped emitting them for new links is
  unverified (math-finite.h removal, around 2.31).
- class: a. links: [S5] NEWS, [S6] libm.abilist.

### I04  2.17 (2012-12-25): clock_gettime family in libc

- change: clock_gettime/clock_getres/clock_nanosleep/... exported from
  libc as GLIBC_2.17; in 2.30 "removed from the librt library for new
  applications" (NEWS 2.30).
- who broke: new binaries on older glibc (classic Ubuntu-14.04-built
  binary on RHEL 6). class: a. links: [S5], [S6].

### I05  2.24 (2016-08-04): libio vtable validation

- change: BZ 20191 (commit db3476aff19b, 2016-06-23) puts all stdio
  vtables in a read-only section and aborts on any other vtable pointer
  ("Fatal error: glibc detected an invalid stdio handle").
- who broke: pre-glibc-2.1-era binaries and libraries that interpose
  _IO_2_1_stdout_ with their own vtable, e.g. libstdc++-libc6.2-2.so.3
  users (BZ 23313, fixed 2.28, 2018-06-26) and a Counter-Strike
  dedicated server on CentOS 7 (BZ 25203, fixed 2.31 and backported;
  reported 2019-11-18). Also Ruby's test suite when libc was loaded by
  absolute path (two libc copies; Ruby bug 12666, 2016-08).
- soname: no. symver: no.
- class: b (fixed twice by glibc itself, over three years).
- links: [S11] BZ 20191, [S12] BZ 23313, [S13] BZ 25203.

### I06  2.25 (2017-02-01): malloc_get_state/malloc_set_state

- change: NEWS 2.25: "The malloc_get_state and malloc_set_state
  functions have been removed. Already-existing binaries that
  dynamically link to these functions will get a hidden implementation
  in which malloc_get_state is a stub ... this change will not adversely
  affect already-built Emacs executables". Same release adds
  getrandom/getentropy/explicit_bzero@GLIBC_2.25.
- who broke: documented as none for old binaries; new binaries using
  getrandom on older glibc (class a). See I19 for the 2.34 sequel.
- class: a (documented b). link: [S5].

### I07  2.26 (2017-08-02): documented behaviour changes, no known hit

- pthread_spin_unlock now a plain release store ("may affect odd fringe
  uses"); Sun RPC and NIS deprecated, libnsl only as compat .so;
  xlocale.h removed (compile-time); stack_t/ucontext_t C++ mangling
  changed (compile/link-time). No verified prebuilt-binary breakage.
- class: c (documented). link: [S5].

### I08  2.27 (2018-02-01): new float libm, static locales, matherr

- change: "Optimized generic expf, exp2f, logf, log2f, powf, sinf, cosf
  and sincosf" (results change in the last bit); statically linked
  programs "will fail and fall back to the builtin C/POSIX locale" with
  2.27 locale data; SVID matherr handling kept only for old binaries;
  the 2.95-era `_IO_*` internals declared unsupported "Unlike other
  symbol removals, these old applications will not be supported using
  compatibility symbols" (whether they were actually removed later:
  unverified). 30 new libc symbols (glob, memfd_create, copy_file_range),
  421 libm symbols (_FloatN).
- who broke: static binaries lose locales (documented); bit-exact
  float test suites (no primary source found: unverified).
- class: c (+a). link: [S5].

### I09  2.28 (2018-08-01): sticky EOF

- change: "All stdio functions now treat end-of-file as a sticky
  condition ... This corrects a longstanding C99 conformance bug ...
  (Bug #1190.)"
- who broke: cups-filters / foomatic-rip: print jobs stuck as
  pending-held on Fedora 29 with glibc 2.27.9000-10; glibc maintainers
  "will not revert this change"; cups-filters patched 2018-10-02.
- soname: no. symver: no. class: c.
- links: [S14] RH 1628255, [S5] NEWS.

### I10  2.28 (2018-08-01): fcntl64, statx, renameat2, C11 threads

- 28 symbols in the GLIBC_2.28 node; "Optimized generic exp, exp2, log,
  log2, pow" (double, results change). class: a (+c for float results,
  unverified real hit). links: [S5], [S6].

### I11  2.29 (2019-02-01): tcache double-free detection; exp@GLIBC_2.29

- change: tcache entries gain a key field; a second free of the same
  chunk now aborts with "free(): double free detected in tcache 2".
  Separately exp/exp2/log/log2/pow get new GLIBC_2.29 nodes (the new
  implementations do not do SVID error handling for new links).
- who broke: programs with latent double frees that had run for years
  (Debian bugs 926386, 1050208 and many upstream issue trackers in
  2019-2020; peek, pytorch, ...). Same shape as I01: undefined
  behaviour that used to be tolerated now aborts.
- class: c (abort) and a (exp@GLIBC_2.29 not found on 18.04).
- links: [S15] Debian 926386, [S6] libm.abilist.

### I12  2.30 (2019-08-01): copy_file_range, librt clock_* for new links

- "The copy_file_range function fails with ENOSYS if the kernel does not
  support the system call ... Previously, user space emulation was
  performed"; clock_* removed from librt for new links; sysctl
  deprecated. No verified hit. class: c (documented). link: [S5].

### I13  2.31 (2020-02-01): gettimeofday tz, settimeofday, dlopen ctor

- gettimeofday's struct timezone now zeroed; settimeofday(tv, tz) with
  both non-null fails EINVAL; "If a lazy binding failure happens during
  dlopen, during the execution of an ELF constructor, the process is
  now terminated. Previously, the dynamic loader would return NULL from
  dlopen"; time64 syscalls are tried first on 32-bit ("Seccomp sandboxes
  are affected by this issue"). No verified prebuilt-binary hit found.
- class: c (documented). link: [S5].

### I14  2.32 (2020-08-05): sysctl always ENOSYS; sys_errlist compat

- sysctl() kept "as a compatibility symbol ... but always fails with
  ENOSYS"; sys_errlist/sys_siglist "exported solely as compatibility
  symbols"; strerror and strerror_l share one buffer; weak-reference
  thread detection declared obsolescent, __libc_single_threaded added
  (GLIBC_2.32; libstdc++ from GCC 11 references it, so GCC-11-built C++
  binaries need glibc >= 2.32: class a).
- class: c / a. links: [S5], [S6].

### I15  2.33 (2021-02-01): faccessat2 under old seccomp filters

- change: access()/faccessat() try faccessat2 first; old runc/Docker
  (< 20.10) and systemd-nspawn (< 247) seccomp profiles return EPERM,
  not ENOSYS, for unknown syscalls, and glibc treated EPERM as final.
- who broke: `test -x` in bash, R ("R_HOME not found"), package builds
  in containers; Arch under systemd-nspawn (FS#69563). Red Hat bug
  1900021 opened 2020-11-20 with the Fedora 34 snapshot, closed NOTABUG
  2021-05-04 (Weimer: "caused by the container engine handling system
  calls incorrectly"). Fedora and OpenEmbedded carried an
  EPERM-as-ENOSYS workaround patch.
- soname: no. symver: no. class: c.
- links: [S16] RH 1900021, [S17] Arch FS#69563.

### I16  2.33 (2021-02-01): stat family becomes real symbols

- stat/fstat/lstat/fstatat/mknod... exported as GLIBC_2.33 (previously
  inline wrappers over __xstat). Any binary built on 2.33+ that calls
  stat fails on older glibc. class: a. link: [S6].

### I17  2.34 (2021-08-02): libpthread/libdl/librt/libutil/libanl merge

- change (NEWS): "all functionality formerly implemented in the
  libraries libpthread, libdl, libutil, libanl has been integrated into
  libc ... Applications which have been linked against glibc 2.33 or
  earlier continue to load the corresponding shared objects (which are
  now empty) ... This can cause applications that contain weak
  references to take unexpected code paths ... potentially exposing
  application bugs." 212 symbols in the GLIBC_2.34 node, including
  __libc_start_main, so every binary linked on 2.34+ needs 2.34+.
- who broke: (a) every "GLIBC_2.34 not found" report since 2021
  (Ubuntu 22.04 builds on 20.04, etc.); (c) programs using the weak
  pthread_create trick now always take the threaded path (benign in the
  reports found).
- soname: no (stub libpthread.so.0 etc. still exist, so NEEDED
  resolves). symver: yes for (a).
- class: a (+c). links: [S5] NEWS 2.34, [S3] Weimer, [S6] abilist.

### I18  2.34 (2021-08-02): GLIBC_PRIVATE exports removed

- change: elf/Versions at 2.33 exports __libc_dlopen_mode, __libc_dlsym,
  __libc_dlvsym, __libc_dlclose, _dl_sym, _dl_vsym under GLIBC_PRIVATE;
  at 2.34 none of them remain; nptl/Versions likewise drops
  __libc_alloca_cutoff (checked in git) [S18].
- who broke: apitrace's glxtrace.so: "undefined symbol: __libc_dlsym,
  version GLIBC_PRIVATE" (issue #811, 2022-06-28, glibc 2.35 on
  Manjaro ARM) [S19]. Same mechanism whenever an old libpthread.so.0 /
  libdl.so.2 from another glibc build meets a new libc.so.6 (Debian
  817960, 2016: h_errno@GLIBC_PRIVATE renamed in 2.22) [S24].
- soname: no. symver: "late": the loader reports the missing symbol at
  dlopen / startup, but a NEEDED-only check passes.
- class: b.

### I19  2.34 (2021-08-02): malloc debug surface moved out of libc.so.6

- change: malloc_get_state@GLIBC_2.2.5 and malloc_set_state@GLIBC_2.2.5
  are present in the 2.33 x86_64 libc.abilist and absent in 2.34; they
  now live only in libc_malloc_debug.so [S6]. __malloc_hook/__free_hook
  etc. stay exported but "no longer have any effect"; MALLOC_CHECK_,
  mtrace() and mcheck() "disabled by default in the main C library"
  unless libc_malloc_debug.so is preloaded (NEWS 2.34).
- who broke: pre-2.25 unexec'd Emacs binaries (need LD_PRELOAD);
  hook-based allocators/tracers silently do nothing; test scripts using
  MALLOC_CHECK_ silently lose the checks. No dated primary bug report
  found for a specific product: unverified as to impact.
- soname: no. symver: "late" for the removed symbols (lookup error at
  load), no for the inert hooks. class: b / c. link: [S5].

### I20  2.34 (2021-08-02): clone3 under old Docker seccomp

- change: pthread_create/fork use clone3 when available; EPERM (what
  Docker <= 20.10.9's default profile returned for syscalls it did not
  know) is not treated as "fall back to clone".
- who broke: Fedora 35 and Ubuntu 21.10 images on older Docker:
  "curl: (6) getaddrinfo() thread failed to start", any threaded
  program. moby issue 42680 (2021-07-27), fixed in Docker 20.10.10 by
  returning ENOSYS for clone3.
- soname: no. symver: no. class: c.
- links: [S20] moby 42680, [S21] Suda write-up.

### I21  2.35 (2022-02-03): glibc registers rseq

- change: NEWS 2.35: automatic thread registration with the rseq
  syscall, exposed as __rseq_offset/__rseq_size/__rseq_flags. In
  nptl/pthread_create.c the registration for a new thread is
  `__libc_fatal ("Fatal glibc error: rseq registration failed\n")` if
  it fails while the main thread had succeeded; the comment says
  "Without seccomp filters, rseq registration will either always fail
  or always succeed" [S22]. Still the same at HEAD.
- who broke: tcmalloc: glibc registers first, tcmalloc's own
  registration fails, per-CPU caches silently fall back to per-thread
  mode (google/tcmalloc #144; MongoDB documents
  GLIBC_TUNABLES=glibc.pthread.rseq=0) [S23]. Elastic Beats crashed with
  the fatal error above on glibc 2.35 (elastic/beats #30576,
  2022-02-24); the Beats/Go thread did not conclusively identify the
  filter, but the code path above requires exactly a post-startup
  seccomp filter that blocks rseq: unverified which one.
- soname: no. symver: no. class: c.

### I22  2.35 (2022-02-03): r_debug r_version, catchsegv, LD_AUDIT

- NEWS: "The r_version update in the debugger interface makes the glibc
  binary incompatible with GDB binaries built without" two named
  commits "when audit modules or dlmopen are used"; catchsegv and
  libSegFault.so removed; audit interface LAV_CURRENT bumped.
- who broke: old GDB builds (documented), scripts using catchsegv.
  class: b. link: [S5].

### I23  2.36 (2022-08-01): DT_HASH dropped from libc.so.6

- change: commit e47de5cb2d4d (Weimer, 2022-04-29) "Do not use
  --hash-style=both for building glibc shared objects": libc.so.6 only
  carries DT_GNU_HASH. Not mentioned in NEWS.
- who broke: Epic Easy Anti-Cheat (parsed libc.so.6's DT_HASH itself):
  Elden Ring, MultiVersus, Blood Hunt, Rogue Company failed to launch
  on Arch (Aug 2022) and Fedora 37; libstrangle and "other game
  libraries". Fedora restored DT_HASH via LDFLAGS in glibc-2.36-7.fc37
  (2022-10-19); EAC shipped a fix in EOS SDK 1.15.2; Arch shipped a
  glibc-eac package.
- soname: no. symver: no (no symbol involved).
- class: b. links: [S25] RH 2129358, [S26] Phoronix 2022-08-13, [S27]
  commit.

### I24  2.36 (2022-08-01): dlsym(RTLD_NEXT) prefers default version

- change: BZ 14932, commit efa7936e4c91 (2022-05-27): RTLD_NEXT now
  returns foo@@v2 like RTLD_DEFAULT instead of the oldest foo@v1.
- who broke: sanitizer runtimes (ASan/TSan) whose __pthread_mutex_lock
  interceptor now gets NULL from dlsym(RTLD_NEXT) on glibc >= 2.36, so
  programs preloading pthread_mutex_lock (rr, ClickHouse) loop forever
  (llvm-project #59820, 2023-01-04). The 2.34 merge had already made
  __pthread_mutex_lock a non-default version.
- soname: no. symver: no. class: c.
- links: [S28] BZ 14932, [S29] LLVM 59820.

### I25  2.36 (2022-08-01): arc4random and mount API nodes

- arc4random/arc4random_buf/arc4random_uniform, fsopen/fsmount/
  move_mount/..., pidfd_open etc. as GLIBC_2.36; LD_ASSUME_KERNEL and
  the kernel version check removed. class: a. links: [S5], [S6].

### I26  2.37 (2023-02-01): legacy hwcaps subdirectories

- NEWS: "The dynamic linker no longer loads shared objects from the
  "tls" subdirectories on the library search path or the subdirectory
  that corresponds to the AT_PLATFORM system name, or employs the
  legacy AT_HWCAP search mechanism". No verified hit (vendors that used
  tls/ also shipped a top-level copy). class: c (documented). [S5]

### I27  2.38 (2023-07-31): libcrypt off by default; C23 scanf/strtol

- NEWS: "libcrypt is no longer built by default"; strlcpy/strlcat and
  the __isoc23_* family added (GLIBC_2.38). GCC 15 defaults to
  -std=gnu23, so anything built with GCC 15 on glibc >= 2.38 pulls
  __isoc23_strtol@GLIBC_2.38 and needs 2.38 at run time.
- libcrypt: this is the ONE case in the survey where a soname check can
  see something, and only at the distribution level. libxcrypt installs
  libcrypt.so.1 with "exactly the same symbol versions as were used by
  glibc" when built with the obsolete API (Fedora 28, Debian 11+), or
  libcrypt.so.2 (XCRYPT_2.0 only) with --disable-obsolete-api (Arch,
  which ships a separate libxcrypt-compat for .so.1). A binary with
  NEEDED libcrypt.so.1 breaks only if the distro drops the compat
  package, and then the soname check does catch it. Programs linked
  against libxcrypt's .so.1 do not run on glibc's .so.1 (README).
- class: a (isoc23) / b at packaging level (libcrypt).
- links: [S5], [S30] libxcrypt README, [S31] Fedora 28 change page.

### I28  2.39 (2024-01-31): libcrypt removed; qsort revert; ld.so malloc

- NEWS: "libcrypt has been removed from the GNU C Library ...
  libcrypt.so.1 will not be installed"; "The dynamic linker calls the
  malloc and free functions in more cases during TLS access ... This can
  result in an infinite recursion if a malloc replacement library or its
  dependencies use dynamic TLS".
- qsort: introsort landed on master 2023-10-31 (commits 274a46c9b25a,
  03bf8357e829), Qualys reported the non-transitive-comparator memory
  corruption affecting "glibc 1.04 to 2.38" (fixed by b9390ba93676,
  2023-12-04, no CVE), and "stdlib: Reinstate stable mergesort
  implementation on qsort" (709fbd3ec359, 2024-01-15) went in before
  the release because "multiple programs expect" stability (Hyrum's
  law, per the 2.39 announcement). NO released glibc shipped the
  non-stable qsort; the prompt's "2.37" attribution is wrong.
- class: b at packaging level (libcrypt) / c documented (ld.so malloc).
- links: [S5], [S32] qsort.c history, [S33] Qualys, [S34] LWN 960309.

### I29  2.40 (2024-07-22): __rseq_size semantics

- NEWS: "__rseq_size now denotes the size of the active rseq area (20
  bytes initially), not the size of struct rseq (32 bytes initially)."
  2.41 then switched to the extensible rseq ABI (size and alignment from
  the aux vector). Kernel 6.19's cpu_id_start optimisation (2026) broke
  tcmalloc, which overlays struct rseq; resolved on the kernel side with
  a 33-byte / 64-byte-aligned registration signalling the new behaviour
  (LWN 2026-04-30). Whether the 2.40 change alone broke a shipped
  program: unverified.
- class: c. links: [S5], [S35] LWN 1070072.

### I30  2.41 (2025-01-30): dlopen refuses executable-stack objects

- NEWS: "dlopen and dlmopen no longer make the stack executable if a
  shared library requires it, either implicitly because of a missing
  GNU_STACK ELF header ... or explicitly ... Instead, loading such
  objects will fail." Error: "cannot enable executable stack as shared
  object requires: Invalid argument". 2.42 added a compatibility mode to
  the glibc.rtld.execstack tunable.
- who broke (all prebuilt, unchanged binaries): Julia 1.9.4/1.10.8/
  1.11.3 via libopenlibm.so (JuliaLang #57250, 2025-02-03); NVIDIA HPC
  SDK 25.3 libaccdevaux.so; Vintage Story mods (Harmony); Pico
  Technology libpicoipp.so; libx264/libde265 on armhf (Ubuntu 2100297);
  sodium-native; postgresql-pllua autopkgtest (Debian 1096038).
- soname: no. symver: no. class: b.
- links: [S5], [S36] Julia, [S37] Ubuntu 2100297, [S38] Debian
  1096038, [S39] NVIDIA forum.

### I31  2.41 (2025-01-30): abort() and SIGABRT longjmp; rseq extensible

- NEWS: "abort is now async-signal-safe and its implementation makes
  longjmp from the SIGABRT handler always abort if set up with setjmp.
  Use sigsetjmp to keep the old behavior". Also getrandom via vDSO is
  no longer a guaranteed cancellation point. No verified hit.
- class: c (documented). link: [S5].

### I32  2.42 (2025-07-28): termios arbitrary/split speeds

- change: commit 5cf101a85aae (H. Peter Anvin, 2025-06-12) "linux:
  implement arbitrary and split speeds in termios": Bnnn header values
  become plain numbers for new builds, cfget/cfset[io]speed get
  GLIBC_2.42 nodes, tcsetattr now translates through termios2 and sets
  CIBAUD differently.
- who broke without recompilation (BZ 33340, 2025-08-30, reporter
  confirms "the program did stop working with just a glibc upgrade"):
  RepetierHost's SetBaudrate helper (3D printers at 250000 baud),
  every program embedding sigrok libserialport (any rate above 460800),
  Chromium's serial API (issue 456677057), uutils stty via the Rust nix
  crate. Root cause per hpa: programs that mix the raw TCGETS2/TCSETS2
  ioctl hack with tcsetattr. Mitigated in 2.43 by commit 8d999a699361
  (2025-11-18) "clear k_termios.c_cflag & CIBAUD for non-split speed":
  "I had to make an educated guess which way would be more likely to
  break fewer things. Unfortunately, it appears I guessed wrong."
  libserialport itself was patched by hpa.
- soname: no. symver: no for old binaries; yes for new ones (class a on
  older glibc).
- class: c (+a). links: [S40] BZ 33340, [S41] commit 5cf101a8.

### I33  2.42 (2025-07-28): guard pages via MADV_GUARD_INSTALL; termio.h

- pthread_create uses MADV_GUARD_INSTALL on Linux >= 6.13; Valgrind
  needed a new release to run such programs (Valgrind NEWS: "glibc
  2.42+ (with linux 6.13+) uses MADV_GUARD_INSTALL"; separately
  "502126 glibc 2.41 extra syscall_cancel frames"). termio.h removed
  (compile-time only).
- class: c for tools that model glibc internals. links: [S5], [S42].

### I34  2.43 (2026-01-24): dumped heaps gone; libm SVID nodes

- NEWS: "Support for dumped heaps has been removed. malloc_set_state()
  now always returns the error code -1" (the last vestige of I06/I19);
  TX lock elision removed; uimaxabs renamed to umaxabs and fromfp's
  return type changed, both with compat symbols; 52 new libm symbols
  (acosf, asinf, atan2f, coshf, ... moved off SVID wrappers). No
  verified old-binary breakage; new binaries using those float
  functions fail on <= 2.42.
- class: a (+b documented, pre-2.25 Emacs only). links: [S5], [S6].

### I35  2.44 (2026-07-25): nothing for old binaries

- NEWS compatibility section: malloc alignment may be relaxed in the
  FUTURE (doc only), s390 31-bit dropped, --enable-memory-tagging and
  --enable-static-nss removed. cosh/sinh@GLIBC_2.44 in libm. nixpkgs'
  2.44 bump needed source fixes in grub2, krb5, efivar, ldb, ... (build
  time, not run time).
- class: a only. links: [S5], [S43] nixpkgs PR 557451.

Compile-time-only header changes in the window, for completeness (they
matter when we REBUILD on the updated host, not for running prebuilt
binaries): sys/xattr.h vs linux/xattr.h XATTR_CREATE redefinition
(2.16 era, fixed in kernel 3.13/3.17 uapi headers; Arch FS#38387)
[S44]; xlocale.h removed (2.26); sys/sysmacros.h no longer implied by
sys/types.h (2.28); sys/sysctl.h removed (2.32); SIGSTKSZ non-constant
(2.34); linux/mount.h vs sys/mount.h (2.36, not in NEWS: unverified
here); termio.h removed (2.42); C23 const-preserving strchr/memchr
macros ("can lead to compilation issues", 2.43).

## 3. Frequency: how often a release carried a class (b) or (c) change

Window: 2.13 (2011-02) through 2.44 (2026-07), 32 releases, exactly two
per year since 2.15.

Releases with a VERIFIED break of real, unmodified, prebuilt software
(a primary bug report naming the program): 10 of 32.

```
2.13  memcpy backwards        Flash, gstreamer plugins        (c)
2.24  libio vtables           old stdio interposers, HL server (b)
2.28  sticky EOF              cups-filters printing            (c)
2.29  tcache double-free      many latent-bug programs         (c)
2.33  faccessat2 EPERM        containers: test -x, R           (c)
2.34  clone3 EPERM; GLIBC_PRIVATE; malloc_set_state
                              containers; apitrace; Emacs      (c,b)
2.35  rseq registration       tcmalloc, Elastic Beats          (c)
2.36  DT_HASH; RTLD_NEXT      EAC games, libstrangle; ASan     (b,c)
2.41  exec-stack dlopen       Julia, NVHPC, x264, pllua, ...   (b)
2.42  termios speeds          RepetierHost, libserialport, ... (c)
```

Releases with a DOCUMENTED (NEWS) class (b)/(c) change for which I found
no primary report of a shipped program breaking: 11 more (2.23
[sched_setaffinity no longer guesses the cpuset size], 2.25, 2.26,
2.27, 2.30, 2.31, 2.32, 2.37, 2.39 [ld.so malloc], 2.40, 2.43).
Counting those, 21 of 32 releases (66 percent) changed observable
behaviour of an old binary without any soname or symbol version moving.

Releases with NO class (b)/(c) entry at all: 2.14, 2.15, 2.16, 2.17,
2.18, 2.19, 2.20, 2.21, 2.22, 2.38 (libcrypt is packaging), 2.44: 11.
Note the cluster: 2.14 through 2.22 (2011-2015) is quiet because NEWS
did not yet have the compatibility section and because the project was
smaller; from 2.24 on, 19 of 21 releases carry something.

Rate of the verified list: 10 hits in 15.5 years, i.e. one every ~19
months, but 7 of the 10 are in the last five years (2.33 through 2.42),
so the recent rate is closer to one per year. Reasons visible in the
record: glibc now adopts new syscalls aggressively (faccessat2, clone3,
rseq, MADV_GUARD_INSTALL, termios2) and sandboxes lag; the loader has
been tightening (vtable validation, DT_HASH, exec-stack, RTLD_NEXT);
and each tightening exposes something that "worked" for 15 years.

Two patterns dominate the verified breaks and both are invisible to any
static check:

1. glibc starts using a newer kernel interface and the program's
   environment (seccomp, old runtime) answers with the wrong errno
   (2.33, 2.34, 2.35).
2. glibc removes a tolerance for undefined or undocumented behaviour
   (2.13, 2.24, 2.29, 2.36, 2.41, 2.42).

Class (a), for comparison, is present in nearly every release (see the
node counts in section 1): 26 of 32 releases added at least one libc
node on x86_64, and 2.34 added 212. This is by design and is exactly
what the symbol-version check is for.

## 4. What this means for a "NEEDED sonames still resolve" check

What the check sees:

- A library package removed from the host entirely, or an soname bump
  of a non-glibc library. For glibc that has happened once in the
  window, indirectly: libcrypt.so.1 disappears if the distro stops
  shipping libxcrypt's compat build (I27/I28). libpthread.so.0,
  libdl.so.2, librt.so.1, libutil.so.1, libanl.so.1 still exist as
  stubs after 2.34, so the merge is invisible to it.

What the check does not see, with the incident ids:

- Class (b) with the symbol still present: DT_HASH (I23), exec-stack
  refusal (I30), vtable validation (I05). The file resolves, the loader
  or a plugin load fails later.
- Class (b) with a symbol gone: GLIBC_PRIVATE (I18), malloc_set_state
  (I19). NEEDED resolves; the dynamic linker fails at startup (BIND_NOW)
  or at first call (lazy). `ldd -r` or `LD_BIND_NOW=1` would surface
  these; a soname/ldconfig lookup does not.
- Class (c), all of it (I01, I09, I11, I15, I20, I21, I24, I32 and the
  documented ones). Nothing on disk changes for the binary; only its
  behaviour does.
- Class (a) is not in scope for "old binary, updated host" but note
  that a NEEDED-only check also misses it: `ldd` does report "version
  GLIBC_2.34 not found" because it runs the loader's version check, a
  parser of DT_NEEDED against ldconfig -p does not.

Concretely for the tree in question (GCC, Python, binutils, meson,
ninja, mtools, ncurses tools dynamically linked against the host
glibc): every glibc release from 2.33 through 2.43 except 2.38 (and
2.44 so far) has contained at least one change that passes a soname
check and alters or breaks the behaviour of some unchanged binary; in
seven of those releases the victim is named in a bug report. The class
(c) items that touch the
kinds of programs we ship are the seccomp/errno ones (any sandboxed CI
runner), the malloc hardening ones (a latent double free in an old
Python C extension aborts after the update), and the loader tightening
ones (a Python wheel or GCC plugin with a missing GNU_STACK note stops
loading on 2.41). None of these are hypothetical; each has a dated bug
above.

Practical conclusion from the record: the only test that detects the
classes glibc actually breaks is execution. Record the host glibc
version the tree was validated against (`getconf GNU_LIBC_VERSION`),
treat a change of that value as invalidating the validation, and rerun
a smoke test that exercises each tool end to end (compile+link+run a
program with the prebuilt GCC, import a C extension with the prebuilt
Python, build a trivial meson/ninja project). A soname check remains
useful as the cheap first filter for the non-glibc libraries, where
soname bumps are the norm rather than the exception.

## 5. Sources

Notation: `GW` stands for `https://sourceware.org/git/?p=glibc.git` and
`ABI` for `sysdeps/unix/sysv/linux/x86_64/64`; gitweb accepts the 12-char
abbreviated hashes. Two Red Hat Developer URLs that cannot be shortened
are wrapped inside angle brackets per RFC 3986 Appendix C (remove the
line break and leading spaces to reconstruct them).

- [S1] glibc wiki FAQ, "What is symbol versioning good for?"
  https://sourceware.org/glibc/wiki/FAQ
- [S2] DJ Delorie, "How the GNU C Library handles backward
  compatibility", Red Hat Developer, 2019-08-01.
  <https://developers.redhat.com/blog/2019/08/01/
   how-the-gnu-c-library-handles-backward-compatibility>
- [S3] Florian Weimer, "Why glibc 2.34 removed libpthread", Red Hat
  Developer, 2021-12-17.
  <https://developers.redhat.com/articles/2021/12/17/
   why-glibc-234-removed-libpthread>
- [S4] glibc wiki, Release process and Glibc Timeline (release dates).
  https://sourceware.org/glibc/wiki/Release
  https://sourceware.org/glibc/wiki/Glibc%20Timeline
- [S5] glibc NEWS, git HEAD (through 2.44, 2.45 stub).
  https://sourceware.org/git/?p=glibc.git;a=blob_plain;f=NEWS;hb=HEAD
- [S6] x86_64 ABI lists (HEAD, 2.33, 2.34 tags).
  GW;a=blob_plain;f=ABI/libc.abilist;hb=HEAD
  GW;a=blob_plain;f=ABI/libm.abilist;hb=HEAD
  GW;a=blob_plain;f=ABI/libc_malloc_debug.abilist;hb=glibc-2.34
- [S7] Red Hat bug 638477 "Strange sound on mp3 flash website"
  (2010-09-29). https://bugzilla.redhat.com/show_bug.cgi?id=638477
- [S8] glibc BZ 12518 "memcpy acts randomly (and differently) with
  overlapping areas" (Torvalds, 2011-02-25).
  https://sourceware.org/bugzilla/show_bug.cgi?id=12518
- [S9] LWN, "Glibc change exposing bugs", 2010-11-10.
  https://lwn.net/Articles/414467/
- [S10] glibc commit 0354e355014b7bfda32622e0255399d859862fcd
  (2011-04-01), memcpy@GLIBC_2.14.
  GW;a=commit;h=0354e355014b
- [S11] glibc BZ 20191 "libio: vtables hardening" (2.24).
  https://sourceware.org/bugzilla/show_bug.cgi?id=20191
- [S12] glibc BZ 23313 "libio vtables validation and standard file
  object interposition" (fixed 2.28).
  https://sourceware.org/bugzilla/show_bug.cgi?id=23313
- [S13] glibc BZ 25203 "Disable libio vtable validation for interposed
  pre-2.1 stdio handles" (fixed 2.31).
  https://sourceware.org/bugzilla/show_bug.cgi?id=25203
- [S14] Red Hat bug 1628255, cups-filters vs sticky EOF (2018-09-12).
  https://bugzilla.redhat.com/show_bug.cgi?id=1628255
- [S15] Debian bug 926386 "free(): double free detected in tcache 2".
  https://bugs.debian.org/926386
- [S16] Red Hat bug 1900021, faccessat under restrictive seccomp
  (2020-11-20). https://bugzilla.redhat.com/show_bug.cgi?id=1900021
- [S17] Arch FS#69563, glibc 2.33 under systemd-nspawn.
  https://bugs.archlinux.org/task/69563
- [S18] elf/Versions and nptl/Versions at glibc-2.33 vs glibc-2.34.
  GW;a=blob_plain;f=elf/Versions;hb=glibc-2.33
  GW;a=blob_plain;f=elf/Versions;hb=glibc-2.34
  GW;a=blob_plain;f=nptl/Versions;hb=glibc-2.33
- [S19] apitrace issue 811, "undefined symbol: __libc_dlsym, version
  GLIBC_PRIVATE" (2022-06-28).
  https://github.com/apitrace/apitrace/issues/811
- [S20] moby issue 42680, seccomp blocks clone3 with EPERM
  (2021-07-27). https://github.com/moby/moby/issues/42680
- [S21] Akihiro Suda, "ubuntu:21.10 and fedora:35 do not work on the
  latest Docker (20.10.9)".
  https://github.com/AkihiroSuda/clone3-workaround (same author's
  write-up: medium.com/nttlabs, post id 1cd439d9921)
- [S22] nptl/pthread_create.c at glibc-2.35 (rseq fatal path, lines
  370-375 and 684-687).
  GW;a=blob_plain;f=nptl/pthread_create.c;hb=glibc-2.35
- [S23] google/tcmalloc issue 144 "Add support for use of GLIBC's
  rseq"; elastic/beats issue 30576 (2022-02-24).
  https://github.com/google/tcmalloc/issues/144
  https://github.com/elastic/beats/issues/30576
- [S24] Debian bug 817960, GLIBC_PRIVATE mismatch after partial
  upgrade (2016-03-12).
  https://lists.debian.org/debian-glibc/2016/03/msg00153.html
- [S25] Red Hat bug 2129358 "glibc 2.36+ breaks EAC with removal of
  DT_HASH" (2022-09-23).
  https://bugzilla.redhat.com/show_bug.cgi?id=2129358
- [S26] Phoronix, "Glibc 2.36 Dropping DT_HASH Has Been Breaking Easy
  Anti Cheat Games", 2022-08-13.
  https://www.phoronix.com/news/Glibc-2.36-EAC-Problems
- [S27] glibc commit e47de5cb2d4dbecb58f569ed241e8e95c568f03c
  (2022-04-29) "Do not use --hash-style=both".
  GW;a=commit;h=e47de5cb2d4d
- [S28] glibc BZ 14932 dlsym RTLD_NEXT (fixed 2.36, commit
  efa7936e4c91b1c260d03614bb26858fbb8a0204).
  https://sourceware.org/bugzilla/show_bug.cgi?id=14932
- [S29] llvm-project issue 59820 (2023-01-04).
  https://github.com/llvm/llvm-project/issues/59820
- [S30] libxcrypt README (soname, obsolete API, compatibility).
  https://github.com/besser82/libxcrypt/blob/develop/README.md
- [S31] Fedora 28 change "Replace glibc libcrypt with libxcrypt".
  https://fedoraproject.org/wiki/Changes/Replace_glibc_libcrypt_with_libxcrypt
- [S32] stdlib/qsort.c history at glibc-2.39.
  GW;a=history;f=stdlib/qsort.c;hb=glibc-2.39
- [S33] Qualys advisory, qsort() out-of-bounds, 2024-01-30.
  https://www.qualys.com/2024/01/30/qsort.txt
- [S34] LWN, GNU C Library 2.39 release announcement.
  https://lwn.net/Articles/960309/
- [S35] LWN, "Restartable sequences, TCMalloc, and Hyrum's Law",
  2026-04-30. https://lwn.net/Articles/1070072/
- [S36] JuliaLang issue 57250 (2025-02-03).
  https://github.com/JuliaLang/julia/issues/57250
- [S37] Ubuntu bug 2100297, dlopen(libx264/libde265) on armhf.
  https://bugs.launchpad.net/ubuntu/+source/glibc/+bug/2100297
- [S38] Debian bug 1096038, postgresql-pllua vs glibc 2.41.
  https://bugs.debian.org/1096038
- [S39] NVIDIA forum, NVHPC 25.3 libaccdevaux.so on glibc 2.41.
  https://forums.developer.nvidia.com/t/331308
- [S40] glibc BZ 33340 "Possible regression in 2.42 termios refactoring
  re non-standard baud rate" (2025-08-30, fixed 2.43).
  https://sourceware.org/bugzilla/show_bug.cgi?id=33340
- [S41] glibc commit 5cf101a85aae0d703cdd8ed7b25fe288e41fdacb
  (2025-06-12) and 8d999a6993611d375adc9efc5369c7bb7963b716
  (2025-11-18).
  GW;a=commit;h=5cf101a85aae
  GW;a=commit;h=8d999a699361
- [S42] Valgrind NEWS (glibc 2.41/2.42 entries).
  https://sourceware.org/git/?p=valgrind.git;a=blob_plain;f=NEWS;hb=HEAD
- [S43] nixpkgs PR 557451 "glibc: 2.42-84 -> 2.44-25".
  https://github.com/NixOS/nixpkgs/pull/557451
- [S44] Arch FS#38387 {linux,sys}/xattr.h incompatibility; libc-alpha
  thread 2014-01. https://bugs.archlinux.org/task/38387
  https://sourceware.org/ml/libc-alpha/2014-01/msg00075.html

Access note: sourceware.org serves an Anubis challenge to browser-like
user agents; everything from it above was fetched with plain curl and
read locally (NEWS, abilists, Versions files, bugzilla pages, gitweb
commits).

## 6. What I could not verify

- Prompt items that do not correspond to anything in the record:
  - "qsort behaviour change in 2.37 and its reversion": the change was
    on master between 2023-10-31 and 2024-01-15 and never released;
    there is no 2.37 qsort change and no CVE (Qualys and glibc agreed
    not to assign one).
  - "2.41/2.42 __libc_start_main changes": the x86_64 abilist has only
    __libc_start_main@GLIBC_2.2.5 and @GLIBC_2.34; no 2.41/2.42 change
    found.
  - "2.41 dlopen TLS": the only 2.41 dlopen change in NEWS is the
    exec-stack refusal; the TLS-related loader change is the 2.39 one
    (ld.so may call malloc during TLS access). No 2.41 TLS regression
    with a primary source found.
  - "printf %n and fortify changes": the writable-format-string %n
    abort dates from the original _FORTIFY_SOURCE work (2.3.4, 2004);
    no change in the 2.13-2.44 window found.
  - "_IO_stdin_used": that is glibc 2.1's (1999) marker for pre-2.1
    stdio layouts; outside the window. The related in-window items are
    I05 (vtable validation) and the 2.27 deprecation of `_IO_*`
    internals; whether those internals were actually removed from the
    export list later is unverified.
- Which glibc release stopped emitting `__*_finite` references for new
  links (I03): believed 2.31, not checked against a commit.
- Real-world breakage from last-bit changes in libm results (I08, I10):
  no primary bug report found; treated as documented (c) only.
- The exact seccomp filter that made Elastic Beats hit the rseq fatal
  path (I21): the code path is verified from glibc source, the Beats
  side is not.
- Impact of the 2.34 malloc hook and MALLOC_CHECK_ neutering (I19) on
  a named shipped product: no dated bug found; the symbol removal of
  malloc_get_state/malloc_set_state is verified from the abilists.
- linux/mount.h vs sys/mount.h conflict attributed to 2.36 and the
  2.37 legacy-hwcaps removal breaking anything: not verified.
- Exact first date of the Arch EAC reports (early Aug 2022 per
  Phoronix 2022-08-13); the Arch GitLab issue itself is behind Anubis
  and was not read.
