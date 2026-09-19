# GCC runtime libraries: same-soname ABI and behaviour changes, 2015-2025

Research note for the question: is "every NEEDED soname still resolves on
this machine" a sufficient test that a prebuilt binary (our GCC, Python,
binutils, meson, ninja, gperf) still works after the host distro updated?
This note covers libstdc++.so.6, libgcc_s.so.1 and the other GCC runtime
libraries (libgomp, libatomic, libquadmath, libitm, libssp, the sanitizer
runtimes). glibc is out of scope.

Every claim below is tied to a numbered source in section 5. Anything
without a primary source is listed in section 6 as unverified.

## 0. Terms used in this note

GCC's own ABI page uses "forward compatible" for the case most people
call backward compatible. To avoid that trap this note uses directions:

- old-on-new: a binary built against an OLDER library runs against a
  NEWER library. This is the direction GCC promises.
- new-on-old: a binary built against a NEWER library runs against an
  OLDER library. GCC does not promise this; it is what the loader's
  symbol-version check is designed to reject.

The incident classes requested by the question, mapped onto that:

- (a) new-on-old failure only. Same soname. Detected at load time by
  ld.so IF the binary references a symbol version the old library
  lacks ("version `GLIBCXX_3.4.32' not found").
- (b) old-on-new failure. Same soname, no new symbol version involved;
  an existing binary stops working after a library upgrade. Nothing
  static detects this.
- (c) behaviour change with no ABI change. Same soname, same symbols,
  different result. Nothing static detects this.
- (d) header-level layout change. Only matters when OBJECTS built by
  different compiler versions are mixed (a prebuilt binary plus a
  distro C++ library that exposes the type in its API). Invisible to
  sonames and, unless the type is abi_tag'ed, to symbol versions.
- (r) rebuild-only break: headers changed so old SOURCE no longer
  compiles. No binary impact. Listed for completeness.

## 1. The policy

### 1.1 libstdc++ (libstdc++.so.6)

Primary source: the libstdc++ manual, "ABI Policy and Guidelines" [S1].

The page defines "library API + compiler ABI = library ABI" and states
the promise in one direction only:

> program binaries linked with the initial release of a library binary
> will still run correctly if the library binary is replaced by
> carefully-managed subsequent library binaries.

and, for the other direction:

> It is not possible to take program binaries linked with the latest
> version of a library binary in a release series (with additional
> symbols added), substitute in the initial release of the library
> binary, and remain link compatible.

Changes allowed with only a minor (filename) bump: adding an exported
global or static data member, adding an exported function, adding
symbols through additional instantiations. Changes the page says need a
MAJOR (soname) bump: changes in the compiler ABI, changing the size,
alignment or layout of an exported symbol, deleting an exported symbol,
adding or removing base classes of a type.

Soname history from the same page: libstdc++.so.3 (GCC 3.0), .so.4
(3.1, incompatible), .so.5 (3.2, incompatible), .so.6 from GCC 3.4.0 to
GCC 16.1.0 (libstdc++.so.6.0.34). Every release since 2004 has kept the
soname; layout changes since then were handled by NOT changing the
exported layout and giving the new type a new mangled name instead (the
GCC 5 dual ABI, section 2 row 1). The version nodes added per release,
from [S1] (GLIBCXX / CXXABI):

    5.1  3.4.21 / 1.3.9       10.1 3.4.28 / 1.3.12
    6.1  3.4.22 / 1.3.10      11.1 3.4.29 / 1.3.13
    7.1  3.4.23 / 1.3.11      12.1 3.4.30 / 1.3.13
    7.2  3.4.24               13.1 3.4.31 / 1.3.14
    8.1  3.4.25               13.2 3.4.32
    9.1  3.4.26 / 1.3.12      14.1 3.4.33 / 1.3.15
    9.2  3.4.27               15.1 3.4.34 / 1.3.15 (1.3.16 on one
    9.3  3.4.28                    target, see section 6)
                              16.1 3.4.34 / 1.3.17

Four of those nodes (3.4.24, 3.4.27, 3.4.28, 3.4.32) were added on a
RELEASE BRANCH, so a binary built with 13.2 can fail on a 13.1 library
of the same major version. What each node carries is in the linker
script [S13]; the ones that matter most for "why does nearly every
binary need the newest node" are listed in section 2.

The dual-ABI page [S2] is the second half of the policy: it documents
that GCC 5 re-implemented std::string, std::list, the stringstream
classes, several locale facets and std::ios_base::failure, that the new
versions live in inline namespace std::__cxx11 with the abi_tag
attribute so they get different mangled names, that the macro
_GLIBCXX_USE_CXX11_ABI selects which one the headers declare, and that
mixing "commonly happens when linking to a third-party library that was
compiled with an older version of GCC" and shows up as undefined
references involving std::__cxx11 or [abi:cxx11].

Why the soname was not bumped: Jason Merrill's Red Hat article [S3]:

> Changing the soname caused a lot of pain but is not sufficient to
> deal with changes in symbol ABIs: if you load multiple shared objects
> that depend on different versions of the library, you can still get
> clashes between different versions of the same symbol.

so "the plan for this ABI change has been to leave the soname (and the
existing binary interface) alone, and express the new ABI using
different mangled names."

### 1.2 libgcc_s.so.1

Soname 1 since GCC 3.0 (SHLIB_SOVERSION = 1 in libgcc/config/t-slibgcc
[S34]). Symbol version nodes in libgcc-std.ver.in [S35]: GCC_3.0 ...
GCC_4.8.0, then GCC_7.0.0 (__divmoddi4, __divmodti4), GCC_14.0.0
(_BitInt helpers, __hardcfr_check, __strub_*, nested function pointer
hooks). The x86 map [S36] adds GCC_4.8.0 (__cpu_model,
__cpu_indicator_init), GCC_12.0.0 (_Float16 conversions) and
GCC_13.0.0. No written ABI policy page exists for libgcc; in practice
it follows the same add-only rule and __gcc_personality_v0 has sat in
GCC_3.3.1 unchanged. The unwinder's internals are not part of any
promise, and section 2 row 14 shows that they did change behaviour for
existing callers in GCC 13.

### 1.3 The other runtimes (from the GCC 15 sources)

    library         soname      version nodes (GCC 15)          [S37]
    libgomp         .so.1       OMP_1.0..OMP_5.2, GOMP_1.0..
                                GOMP_6.0, OACC_2.0..2.6,
                                GOMP_PLUGIN_1.0..1.4
    libatomic       .so.1       LIBATOMIC_1.0, 1.1, 1.2
    libquadmath     .so.0       QUADMATH_1.0, 1.1, 1.2
    libitm          .so.1       LIBITM_1.0
    libgfortran     .so.3 (<=6) bumped to .so.4 in GCC 7 and
                                .so.5 in GCC 8 [S38]
    libssp          .so.0       not shipped on glibc/musl distros,
                                see below

libgfortran is the counter-example that shows the project DOES bump a
soname when it decides an ABI is incompatible; libstdc++ and libgcc_s
have chosen not to.

Sanitizer runtimes make no ABI promise. Sonames per release, read from
libsanitizer/*/libtool-version on each release branch [S22a]:

    GCC    libasan  libtsan  libubsan  liblsan  libhwasan
    4.8    0        0        -         -        -
    4.9    1        0        0         0        -
    5      2        0        0         0        -
    6      3        0        0         0        -
    7      4        0        0         0        -
    8, 9   5        0        1         0        -
    10, 11 6        0        1         0        0 (11)
    12-15  8        2        1         0        0

libasan.so.7 and libtsan.so.1 existed only on trunk between July and
September 2021 [S22]. The July 2021 bump commit states the reason:
upstream compiler-rt "changed __sanitizer_kernel_sigset_t and changed
the ABI for function __sanitizer_syscall_post_impl_rt_sigaction"; the
September one lists three more upstream commits. The runtime is merged
from LLVM and its interface with instrumented code is versioned by
soname, not by symbol version.

libssp: GCC's own configure sets TARGET_LIBC_PROVIDES_SSP on glibc 2.4+
and on every musl target [S39], so -fstack-protector code calls the C
library's __stack_chk_fail and libssp.so.0 is never a NEEDED entry on
those systems. Arch has passed --disable-libssp since gcc 4.7.0-1
(2012) [S40]; Fedora's gcc.spec passes --disable-libssp [S41]; Debian's
rules.defs builds libssp only when the target libc does not provide ssp
[S42]. A binary that lists libssp.so.0 was built on a non-glibc target
or against a GCC explicitly configured with --enable-libssp.

## 2. Incident table

Columns: date | GCC | change | soname changed? | symbol version caught
it? ("lnk" = caught at link time, not load time) | class | note
number. "Who broke" is in the notes below the table.

| date    | GCC      | change                         | so? | sv? | cls | n  |
|---------|----------|--------------------------------|-----|-----|-----|----|
| 2015-04 | 5.1      | dual ABI (string, list, ...)   | no  | lnk | d   | 1  |
| 2015-12 | 5.x,6.x  | lib throws OLD ios_failure     | no  | no  | c   | 2  |
| 2017-05 | 7.1-7.3  | lib throws NEW ios_failure     | no  | no  | b   | 3  |
| 2017-06 | 7.2      | GLIBCXX_3.4.24 on branch       | no  | yes | a   | 4  |
| 2018-02 | 7 lgcc   | GCC_7.0.0 __divmoddi4 (i386)   | no  | yes | a   | 5  |
| 2019-05 | 9.1-9.3  | filesystem in .so; 3.4.27/28   | no  | yes | a   | 6  |
| 2014-20 | any      | bundled old .so.6 shadows sys  | no  | yes | a*  | 7  |
| 2020-08 | 11.1     | string::reserve(n) no shrink   | no  | no  | c   | 8  |
| 2020-10 | 11.1     | std::span member order         | n/a | no  | d   | 9  |
| 2021-03 | 11 pre   | futex call_once, reverted      | no  | no  | b*  | 10 |
| 2021-04 | 11.1     | 3.4.29 __throw_bad_array_...   | no  | yes | a   | 11 |
| 2021-09 | 12.1     | libasan 6->8, libtsan 0->2     | YES | n/a | -   | 12 |
| 2021-12 | 12.1     | condvar::wait new sv 3.4.30    | no  | yes | a   | 13 |
| 2023-04 | 13.1+    | libgcc_s btree FDE registry    | no  | no  | b   | 14 |
| 2023-04 | 13.1     | iostream init into .so         | no  | NO  | a!  | 15 |
| 2023-07 | 13.2     | 3.4.32 added to force reject   | no  | yes | a   | 15 |
| 2024-05 | 14.1     | CXXABI_1.3.15 __cxa_call_term  | no  | yes | a   | 16 |
| 2025-01 | n/a      | PyTorch flips CXX11_ABI to 1   | no  | lnk | d   | 17 |
| 2021-04 | 11,12,13 | header include cleanups        | n/a | n/a | r   | 18 |

Notes per row:

1. GCC 5.1 dual ABI [S2][S3][S4]. Old binaries kept working (libstdc++
   .so.6.0.21 exports both sets; 180 __cxx11 string instantiations
   under GLIBCXX_3.4.21 [S13]). What broke was every link that mixed
   objects or libraries built with different _GLIBCXX_USE_CXX11_ABI
   values, as undefined references to std::__cxx11::basic_string etc.
   Distros: Fedora 22 shipped GCC 5 with the headers defaulting to 0
   because FESCo refused even a C++-only mass rebuild for the schedule,
   Fedora 23 flipped the default to 1 and rebuilt; "libstdc++.so
   library was largely the same in F22 and F23" [S5]. Debian made GCC 5
   the default in unstable in August 2015, kept the libstdc++6 package
   name and soname ("Existing C++98 binary packages will continue to
   work"), and renamed every library package whose exported symbols
   contained __cxx11 or B5cxx11 with a v5 suffix (libfoo2 -> libfoo2v5)
   [S6][S7]; Matthias Klose's announcement calls it "hundreds of
   issues" and lowered NMU thresholds to push it through [S8]. The
   symptom for a dlopen'd plugin is the same undefined-symbol failure
   at dlopen (RTLD_NOW) or first call (RTLD_LAZY) rather than at link.
2. Design gap of the dual ABI, PR 66145: the library throws whichever
   ios_base::failure it was built with, so on GCC 5.x and 6.x every
   program compiled with the (default) new ABI that wrote
   `catch (std::ios_base::failure&)` silently fell through to a
   `catch (std::exception&)` or terminated. Arch forum report from
   December 2015 with a two-line reproducer [S10]; the manual's
   wording is "for GCC 5.x and 6.x the library throws the old type"
   [S2]. No soname, no symbol change; class (c) by the question's
   definition (behaviour differs from what the headers promise).
3. Class (b), the cleanest example in the period. r244498 (January
   2017, shipped in GCC 7.1) made the library throw the new type to fix
   row 2. Result: any binary built with the OLD ABI, i.e. every GCC 4.x
   build and everything built with -D_GLIBCXX_USE_CXX11_ABI=0, that
   caught ifstream::failure now died with "terminate called after
   throwing an instance of 'std::ios_base::failure[abi:cxx11]'". PR
   85222, "[7/8 Regression] ABI breakage of __throw_ios_failure by
   r244498", was filed 2018-04-05 by Richard Biener with a GCC 4.8
   compiled reproducer [S11]. Fix (April 2018, trunk and gcc-7-branch):
   a private __ios_failure type deriving from the new type and
   aggregating an old-type object, with a custom type_info whose
   __do_upcast redirects handlers for either type [S12]; the manual
   says 7.1, 7.2 and 7.3 throw the new type and "current releases"
   throw the dual-catchable one [S2]. Window: 7.1 (2017-05) to 7.3
   (2018-01); closed in 7.4 (2018-12) and 8.1.
4. PR 81092: two std::wstring constructors that "should have been in
   GLIBCXX_3.4.23" were exported in 7.2 under a new node 3.4.24 [S13].
   A binary built with 7.2 that uses them needs 6.0.24; the loader
   rejects it on a 7.1 library.
5. libgcc_s: __divmoddi4 lives in GCC_7.0.0 [S35] and GCC 7 emits it
   for combined 64-bit quotient/remainder on 32-bit targets. Godot
   3.0's official 32-bit Linux export failed on Linux Mint 17.3 with
   "libgcc_s.so.1: version `GCC_7.0.0' not found" [S14].
6. GCC 9 moved std::filesystem from the static libstdc++fs.a into
   libstdc++.so (122 filesystem symbols under GLIBCXX_3.4.26 [S13]);
   missing exports were then added on the branch as 3.4.27 (9.2) and
   3.4.28 (9.3) [S13][S15].
7. Not a libstdc++ change but the most frequent real-world failure in
   the period, and the one a soname check gets exactly wrong: a
   program ships its own libstdc++.so.6 and its RPATH or
   LD_LIBRARY_PATH makes it win. The soname RESOLVES; it resolves to
   the wrong copy, and a system library loaded later (Mesa's libLLVM,
   R's Rcpp, anything under conda) fails with "version GLIBCXX_3.4.x
   not found". Steam runtime, 2014, GLIBCXX_3.4.18 needed by
   libLLVM-3.5 [S16]; Julia 1.3.1 bundling GCC 7.2's library, 2020,
   GLIBCXX_3.4.26 needed by Rcpp [S17], fixed in Julia 1.8.4 by a
   loader that probes both copies and dlopens the newer [S17b]; conda
   environments and GLIBCXX_3.4.30 on Ubuntu 22.04 [S18].
8. Class (c) in libstdc++.so itself. GCC 11 implemented P0966: the
   out-of-line basic_string::reserve(size_type) instantiated inside
   libstdc++.so returns early when the request is <= capacity(); the
   old code shrank [S19]. Pre-11 shrink_to_fit() was an inline call to
   reserve(0), so an old binary calling either against a GCC 11+
   library never shrinks again. Applied to both the new and the
   gcc4-compatible string. No user report found (section 6).
9. std::span's two data members were swapped in October 2020 to be
   layout-compatible with struct iovec, PR 95609 [S20]. Header-only
   C++20 type; objects built with GCC 10 and 11 disagree on the layout
   with no diagnostic. Only mixed-object builds are affected.
10. Near miss, kept because it shows the failure mode: the futex-based
    std::call_once committed for GCC 11 turned out not to write
    glibc's fork generation into the pthread_once_t, so old code using
    pthread_once and new code could both enter the active state on the
    same once_flag; reverted 2021-03-12 before 11.1, keeping the new
    symbols exported "so that code already compiled against GCC 11
    can still use them" [S21].
11. GLIBCXX_3.4.29 carries std::__throw_bad_array_new_length, which
    GCC 11's std::allocator calls from the headers [S13]. Consequence:
    almost any C++ program built with GCC 11+ needs 6.0.29, which is
    why "GLIBCXX_3.4.29 not found" is the single most reported new-
    on-old error of the period (rows 15 and 16 are the 13 and 14
    equivalents).
12. Sanitizer soname bumps, section 1.3 [S22].
13. The correct pattern, for contrast: condition_variable::wait lost
    its noexcept so cancellation unwinds through it; the old
    non-throwing entry stayed exported as @GLIBCXX_3.4.11 and the new
    one as @@GLIBCXX_3.4.30, explicitly "to avoid an ABI break for
    existing code linked to the non-throwing definition" [S23].
14. libgcc_s class (b). The commit "eliminate mutex in fast path of
    __register_frame" (authored 2022-03-01, committed to trunk
    2022-09-16, absent from the 12 branch) replaced the sorted list
    behind __register_frame / __deregister_frame / _Unwind_Find_FDE
    with a lock-free b-tree, first shipped in 13.1 [S24]. Regressions
    in that code, all hitting existing callers of an unchanged API:
    PR 110956 (Solaris libraries with unusual encodings, August 2023,
    fixed in 13.2) [S25]; PR 111731 "[13/14 regression] gcc_assert is hit at
    libgcc/unwind-dw2-fde.c#L291", opened 2023-10-08 for unwind tables
    embedded inside the registered code range, reporters from Intel
    and SAP, fixed on the 13 branch 2024-03-11 (13.3) [S26][S27]; PR
    119151 "[13/14/15 Regression] unwind-dw2-btree maintains
    separators wrong", lookups returning NULL after a particular
    insert/remove sequence, fixed 2025-03-10 [S28]. A JIT built years
    ago with any compiler is exposed the day libgcc_s.so.1 is upgraded
    to 13.1 or 13.2.
15. The one case where the loader check has a documented hole. GCC 13
    stopped emitting a std::ios_base::Init object per translation unit
    and initialises the streams inside libstdc++.so [S29]. A 13.1
    binary on a 12 library loads fine and then uses an uninitialised
    std::cout, because no new symbol was referenced. Jakub Jelinek's
    13.2 fix, PR 108969, forces an undefined reference to
    _ZSt21ios_base_library_initv@GLIBCXX_3.4.32 into every object that
    includes <iostream> for no other reason than to make ld.so say
    "version `GLIBCXX_3.4.32' not found" [S30]. This is also why
    3.4.32 is required by almost every C++ binary built with 13.2+.
16. CXXABI_1.3.15 is __cxa_call_terminate alone [S13]; GCC 14 emits
    calls to it for noexcept violations, so nearly every GCC 14 C++
    binary needs libstdc++ 6.0.33 (onnxruntime and Gentoo reports)
    [S31].
17. Ten years after GCC 5 the dual ABI is still a live constraint:
    PyTorch 2.6 (January 2025) built its first Linux wheels with
    CXX11_ABI=1 and asked extension authors to match, planning the
    full switch for 2.7 [S9]. Included as evidence that class (d)
    does not age out.
18. Rebuild-only: GCC 11 headers stopped including <limits>, <memory>,
    <utility>, <thread> transitively; GCC 12 stopped including
    <memory>, <iterator>, <algorithm>, <utility>, <array>, <atomic>,
    <ctime>, <pthread.h>; GCC 13 stopped including <string>,
    <system_error>, <cstdint>, <cstdio>, <cstdlib> [S32][S33][S33b].
    Existing binaries are unaffected.

Checked and NOT found (details in section 6): no same-soname layout
change to std::basic_regex, std::basic_stringbuf or std::string_view
between GCC 5 and GCC 15; the data members of all three are identical
across the release branches [S45].

## 3. Frequency over GCC 5 to 15 (2015 to 2025)

Eleven major releases. Counting only changes with a primary source
that hit software which already existed as a binary:

Class (b), old binary breaks on a newer same-soname runtime: 2
release series.

- GCC 7.1 to 7.3 (libstdc++, 2017-05 to 2018-12): old-ABI binaries
  catching std::ios_base::failure. Fixed in 7.4 / 8.1.
- GCC 13.1 onward (libgcc_s, 2023-04 to 2025-03): programs that call
  __register_frame (JITs) or unwind through such frames. Fixed
  piecemeal in 13.2, 13.3 and the 2025 point releases.

Both were classed as regressions by GCC and fixed inside the same
major series, so the exposure window is "the months between two point
releases of the distro's GCC package", not "forever". Both hit narrow
populations (a catch clause for one exception type; frame
registration by JITs). Neither would have been caught by any static
check of the binary against the library.

Class (c), behaviour change and no ABI change: 2 documented.

- GCC 5.x/6.x: ios_base::failure not catchable from new-ABI code
  (the dual-ABI seam, row 2).
- GCC 11.1+: basic_string::reserve(n) stops shrinking for every
  caller of the shared instantiation (row 8). No victim found.

Class (d), header-level: GCC 5 (dual ABI), GCC 10 to 11 (std::span).
Only matters when mixing objects from different compilers.

Class (a), new-on-old, caught by the loader: every major release
(new node each time) plus the branch additions 7.2, 9.2, 9.3, 13.2.
The 13.1 iostream case is the one class (a) change the loader did NOT
catch until 13.2 patched the hole.

Soname bumps in the window, by the GCC release that made them:
libasan at 5, 6, 7, 8, 10 and 12; libtsan at 12; libubsan at 8;
libgfortran at 7 and 8. libstdc++, libgcc_s, libgomp, libatomic,
libquadmath, libitm: none.

## 4. What this means for a "sonames resolve" check

What the check sees:

- A runtime library that is gone. In this set that is only the
  sanitizer runtimes (the distro's GCC 12 update deletes libasan.so.6
  and installs libasan.so.8), libgfortran.so.3/.4, and libssp.so.0 on
  a system that never had it. For libstdc++.so.6, libgcc_s.so.1,
  libgomp.so.1, libatomic.so.1, libquadmath.so.0 and libitm.so.1 the
  check has had nothing to say in eleven years, because those files
  have always been there.

What the check does not see but ld.so does, at load time:

- Class (a). The binary carries a .gnu.version_r (verneed) table
  naming the exact nodes it needs (GLIBCXX_3.4.32, CXXABI_1.3.15,
  GCC_7.0.0, GOMP_4.0, ...). ld.so compares it against the library's
  .gnu.version_d and refuses to load on a miss. A soname lookup does
  not read either table. The cheap equivalent is to compare `readelf
  -V` verneed of the binary against verdef of the library that
  actually resolves (ldd prints the same "version X not found" line
  because it runs the loader). This is the direction our tree is
  exposed to: our GCC is newer than the host's, every binary it builds
  requires the newest node of its own libstdc++ (rows 11, 15, 16 show
  that this is true of essentially every binary, not just ones using
  new features), and a host whose libstdc++ is older, or a host that
  rolls back, fails at exec. "Soname resolves" passes in that state.
- The one exception is row 15: a GCC 13.1 binary on a GCC 12 library
  passes even the loader check and misbehaves at run time.
- Row 7 is the second thing the check gets wrong: the soname resolves
  to a shadowing copy that is not the one the check looked at.

What nothing static sees:

- Class (b) and (c): two (b) episodes and two (c) changes in eleven
  years, all inside libstdc++.so / libgcc_s.so.1 proper. For the
  binaries in question (GCC's own executables, Python, binutils,
  meson, ninja, gperf) the exposed surfaces are: catching
  std::ios_base::failure with an old-ABI build (ours are new-ABI
  unless the toolchain was configured with
  --with-default-libstdcxx-abi=gcc4-compatible), registering JIT
  frames (only if Python loads an
  extension that does), string shrink semantics (correctness never
  depends on it). The residual risk is small; the point is that the
  check cannot bound it, only a run can. A smoke run of each tool
  after a distro update is the only detector for this class, and it
  is also the only detector for row 15.
- Class (d): only if a prebuilt binary links a distro C++ library
  other than libstdc++ itself. For GCC, Python, binutils, meson,
  ninja and gperf the expected chain is libstdc++, libgcc_s, libm,
  libc plus C libraries (gmp, mpfr, mpc, zlib, libffi ...). If the
  NEEDED lists confirm that no distro C++ library is in the chain,
  the dual ABI and std::span rows do not apply. This was not checked
  against the actual binaries.

Two structural ways to shrink the exposure, both standard practice for
prebuilt distributors: link the tools with -static-libstdc++
-static-libgcc (what Red Hat's devtoolset does with
libstdc++_nonshared.a, and what removes the class (a) direction
entirely), or, if the shared dependency is kept, record the maximum
GLIBCXX/CXXABI/GCC_ node each binary requires at build time and check
that against the host library's verdef, which is the same test ld.so
will perform and is the cheapest test that has actual predictive
value.

## 5. Sources

Primary sources (GCC manual, GCC git, GCC lists, distro packaging
repos, upstream issue trackers) unless marked otherwise.

- S1  libstdc++ manual, ABI Policy and Guidelines
      https://gcc.gnu.org/onlinedocs/libstdc++/manual/abi.html
- S2  libstdc++ manual, Dual ABI
      https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_dual_abi.html
- S3  J. Merrill, GCC5 and the C++11 ABI, Red Hat Developer, 2015-02-05
      https://developers.redhat.com/blog/2015/02/05/gcc5-and-the-c11-abi
- S4  GCC 5 release notes, Runtime Library section
      https://gcc.gnu.org/gcc-5/changes.html
- S5  Law, Wakely, Newsome, Jelinek, Merrill: GCC 5 in Fedora, Red Hat
      Developer, 2015-02-10 (also reposted by Fedora Magazine)
      https://developers.redhat.com/blog/2015/02/10/gcc-5-in-fedora-2
- S6  Debian wiki, GCC5 transition page
      https://wiki.debian.org/GCC5
- S7  M. Klose, preparing for GCC 5/libstdc++6, d-d-a 2015-07-07
      https://lists.debian.org/debian-devel-announce/2015/07/msg00000.html
- S8  M. Klose, Follow-up transitions for the libstdc++6 ABI changes,
      d-d-a 2015-08-03
      https://lists.debian.org/debian-devel-announce/2015/08/msg00002.html
- S9  PyTorch 2.6 release blog, CXX11_ABI paragraph
      https://pytorch.org/blog/pytorch2-6/
- S10 Arch forum, New ABI and std::ios_base::failure, 2015-12-13
      https://bbs.archlinux.org/viewtopic.php?id=206167
- S11 PR libstdc++/85222 initial report (gcc-bugs archive)
      https://www.mail-archive.com/gcc-bugs@gcc.gnu.org/msg570052.html
- S12 J. Wakely, [PATCH] PR libstdc++/85222 allow catching iostream
      errors as gcc4-compatible ios::failure, 2018-04-09
      https://gcc.gnu.org/pipermail/libstdc++/2018-April/046890.html
- S13 libstdc++-v3/config/abi/pre/gnu.ver on the releases/gcc-13,
      gcc-14 and gcc-15 branches of https://github.com/gcc-mirror/gcc
- S14 godotengine/godot issue 16409, 2018-02-05
      https://github.com/godotengine/godot/issues/16409
- S15 GCC 9 release notes (filesystem no longer needs -lstdc++fs)
      https://gcc.gnu.org/gcc-9/changes.html
- S16 ValveSoftware/steam-runtime issue 13, 2014-04-26
      https://github.com/ValveSoftware/steam-runtime/issues/13
- S17 JuliaLang/julia issue 34276, 2020-01-06
      https://github.com/JuliaLang/julia/issues/34276
- S17b JuliaLang/julia PR 46976, Probe and dlopen() the correct
      libstdc++, merged 2022-11-08
      https://github.com/JuliaLang/julia/pull/46976
- S18 isl-org/Open3D issue 5531 (conda, GLIBCXX_3.4.30, Ubuntu 22.04)
      https://github.com/isl-org/Open3D/issues/5531
- S19 gcc commit 140cf935cd, libstdc++: Implement P0966
      std::string::reserve should not shrink, 2020-08-06
      https://github.com/gcc-mirror/gcc/commit/140cf935cd
- S20 gcc commit 0f7cd5e573, libstdc++: Make std::span
      layout-compatible with struct iovec [PR 95609], 2020-10-28
      https://github.com/gcc-mirror/gcc/commit/0f7cd5e573
- S21 J. Wakely, [PATCH 1/2] libstdc++: Revert to old std::call_once
      implementation [PR 99341], 2021-03-12
      https://gcc.gnu.org/pipermail/libstdc++/2021-March/052188.html
- S22 gcc commits 4eea703e7d (2021-07-20) and 984400f04e
      (2021-09-27), libsanitizer: Bump asan/tsan versions
      https://github.com/gcc-mirror/gcc/commit/4eea703e7d
      https://github.com/gcc-mirror/gcc/commit/984400f04e
- S22a libsanitizer/{asan,tsan,ubsan,lsan,hwasan}/libtool-version on
      releases/gcc-4.8 through releases/gcc-15 (raw.githubusercontent)
- S23 gcc commit 9e18a25331, libstdc++: Allow std::condition_variable
      waits to be cancelled [PR103382], 2021-12-07
      https://github.com/gcc-mirror/gcc/commit/9e18a25331
- S24 gcc commit 6e80a1d164, eliminate mutex in fast path of
      __register_frame, 2022-03-01
      https://github.com/gcc-mirror/gcc/commit/6e80a1d164
- S25 PR libgcc/110956 (gcc-bugs archive)
      https://www.mail-archive.com/gcc-bugs@gcc.gnu.org/msg782448.html
- S26 PR libgcc/111731 initial report, 2023-10-08
      https://www.mail-archive.com/gcc-bugs@gcc.gnu.org/msg787257.html
- S27 gcc r13-8555, handle unwind tables that are embedded within
      unwinding code [PR111731], 2024-03-11
      https://www.mail-archive.com/gcc-cvs@gcc.gnu.org/msg00827.html
- S28 gcc commit 21109b37e8, libgcc: Fix up unwind-dw2-btree.h
      [PR119151], 2025-03-10
      https://github.com/gcc-mirror/gcc/commit/21109b37e8
- S29 P. Palka, A leaner <iostream> in libstdc++ for GCC 13, Red Hat
      Developer, 2023-04-03
      https://developers.redhat.com/articles/2023/04/03/leaner-libstdc-gcc-13
- S30 gcc commit 9c9061e041 (releases/gcc-13), libstdc++: Another
      attempt to ensure g++ 13+ compiled programs enforce gcc 13.2+
      libstdc++.so.6 [PR108969], 2023-04-28
      https://github.com/gcc-mirror/gcc/commit/9c9061e041
- S31 microsoft/onnxruntime issue 28080 (__cxa_call_terminate)
      https://github.com/microsoft/onnxruntime/issues/28080
- S32 GCC 11 porting_to
      https://gcc.gnu.org/gcc-11/porting_to.html
- S33 GCC 12 porting_to
      https://gcc.gnu.org/gcc-12/porting_to.html
- S33b GCC 13 porting_to
      https://gcc.gnu.org/gcc-13/porting_to.html
- S34 libgcc/config/t-slibgcc (SHLIB_SOVERSION = 1), releases/gcc-15
- S35 libgcc/libgcc-std.ver.in, releases/gcc-15
- S36 libgcc/config/i386/libgcc-glibc.ver, releases/gcc-15
- S37 libgomp/libgomp.map, libatomic/libatomic.map,
      libquadmath/quadmath.map and the libtool_VERSION lines of the
      corresponding configure.ac files, releases/gcc-4.9 .. gcc-15
- S38 libgfortran/libtool-version, releases/gcc-6, gcc-7, gcc-8
- S39 gcc/configure.ac, gcc_cv_libc_provides_ssp probe, releases/gcc-15
- S40 Arch Linux gcc PKGBUILD history (tags 4.6.x without, 4.7.0-1
      and later with --disable-libssp; current PKGBUILD line 140)
      https://gitlab.archlinux.org/archlinux/packaging/packages/gcc
- S41 Fedora rawhide gcc.spec, --disable-libssp
      https://src.fedoraproject.org/rpms/gcc/raw/rawhide/f/gcc.spec
- S42 Debian gcc-15 debian/rules.defs, with_libssp := libc provides ssp
      https://salsa.debian.org/toolchain-team/gcc (gcc-15-debian branch)
- S43 GCC 12 release notes (zero-width bit-field psABI caveat)
      https://gcc.gnu.org/gcc-12/changes.html
- S44 MaskRay, C++ standard library ABI compatibility, 2023 (secondary)
      https://maskray.me/blog/2023-06-25-c++-standard-library-abi-compatibility
- S45 include/bits/regex.h, include/std/sstream, include/std/
      string_view, include/std/span on releases/gcc-4.9 .. gcc-15,
      data members compared by grep

## 6. What could not be verified

- The number of Debian library packages renamed with the v5 suffix.
  The announcements say "hundreds of issues" [S8] and the wiki lists
  about fifty packages for the system_error subset [S6]; no primary
  total was found. Unverified.
- Which shipping product hit PR 111731 and PR 119151. The reporters
  are from Intel and SAP and the bug text talks about JIT-registered
  frames; no product is named. Unverified.
- A user actually broken by the GCC 11 basic_string::reserve() change
  (row 8). The change and its reach into libstdc++.so are verified
  from the commit; a victim was not found. Unverified.
- Steam's later mitigation (choosing the newer of the runtime and
  system libstdc++ at launch) and Godot's fix for row 5. Both are
  commonly described but were not checked against a primary source.
  Unverified.
- The ABI page lists CXXABI_1.3.16 for GCC 15.1 "for RISC-V only"
  while the GCC 15 linker script shows 1.3.16 holding the typeinfo
  for __bf16 [S13]; the two were not reconciled. Unverified detail.
- That GCC 7.4 was the first 7.x release with the dual-catchable
  ios_failure: inferred from the manual's "7.1, 7.2 and 7.3" wording
  and the presence of __ios_failure on the releases/gcc-7 branch; the
  7.4 release notes were not read. Likely but unverified.
- libatomic's 16-byte AVX ifunc (PR 104688, GCC 12) changes what an
  old binary gets from __atomic_load_16 on a library upgrade. Verified
  from the patch mail; no breakage report exists, so it is not in the
  table.
- std::regex, std::basic_stringbuf, std::string_view: no same-soname
  layout change found from GCC 5 to 15 [S45]. basic_regex did change
  between 4.9 and 5 (a regex_traits member became a locale member),
  but that type moved into std::__cxx11 at the same time, so the two
  layouts have different mangled names. Verified negative.
- Red Hat devtoolset's libstdc++_nonshared.a technique (section 4) is
  stated from general knowledge, not from a fetched primary source.
  Unverified.
- The behaviour of libgomp, libquadmath and libitm for old binaries
  was not researched beyond their symbol version maps; no incident
  was searched for. Not researched.
