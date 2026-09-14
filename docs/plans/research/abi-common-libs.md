# Same-soname ABI and behaviour breaks in common toolchain libraries

Research note, September 2026. Question under study: is "every NEEDED
soname of our prebuilt binaries still resolves on this host" a good
enough test that a binary built earlier still works after the distro
updated its shared libraries? This note collects, per library, the
soname history, whether the project bumps on ABI break, and every
documented case where a library broke dependents without a soname
change. Sources are primary where possible (NEWS files, bug trackers,
the ABI Tracker at abi-laboratory.pro, distro archives). Items that
could not be confirmed are listed in section 6, not silently dropped.

Scope: the host libraries our tree links dynamically: zlib, zstd,
ncurses/tinfo, libffi, libxcrypt, elfutils, flex runtime, tcl/tk,
expat, openssl, libpython, readline, libxml2, sqlite, bzip2, xz,
gmp/mpfr/mpc, isl.

Classes used throughout (from the question):

    a  forward-incompatible only: NEW binary on OLD library. Missing
       symbol or missing symbol-version node; the dynamic loader
       reports it at load (or at first call under lazy binding).
    b  backward-incompatible under the SAME soname: OLD binary on NEW
       library. Removed symbol (loader catches it, soname check does
       not), or changed struct layout / signature (nothing catches
       it; silent misbehaviour or crash).
    c  behaviour change with no ABI change at all.
    d  soname bumped correctly; a soname check sees it.

## 1. Per-library summary

Column "bump" answers "does the project bump the soname when it
breaks the ABI?" ("ofte" = often, "<214" = not before 2.14). "ABI
Tracker" refers to abi-laboratory.pro; its timelines stop between
2020 and 2023 depending on the library, so later versions are covered
from NEWS files and bug reports.

| Library   | sonames over time (year)               | bump | same-soname brk  |
|-----------|----------------------------------------|------|------------------|
| zlib      | libz.so.1 since the 1990s, unchanged   | yes  | c: 1.2.12, s390x |
| zstd      | libzstd.so.1 since 1.0.0 (2016)        | part | b: exp. symbols  |
| ncurses   | .so.5 (2000-15), .so.6 (6.0, Aug 2015) | yes  | data: 6.1 tinfo  |
| libffi    | .so.5 (3.0), .so.6 (2012), .so.7 (3.3, | yes  | none found       |
|           | Nov 2019), .so.8 (3.4, Jun 2021-3.8)   |      |                  |
| libxcrypt | libcrypt.so.1 (glibc-compat build) or  | yes  | none found       |
|           | .so.2 (default; Arch + -compat pkg)    |      |                  |
| elfutils  | libelf/libdw/libasm .so.1 for 15+ yrs, | yes  | none found       |
|           | libdebuginfod.so.1 since 0.178 (2019)  |      |                  |
| flex      | libfl.so.2 (2.6.x, 2015+)              | n/a  | none             |
| tcl       | libtcl8.5.so, libtcl8.6.so (Dec 2012), | yes  | none found       |
|           | libtcl9.0.so (Sep 2024)                |      |                  |
| expat     | libexpat.so.1 since 2.0 (2006)         | yes  | c: 2.4, 2.6, 2.7 |
| openssl   | .so.0.9.8, .so.1.0.0 (2010), .so.1.1   | most | b: 1.0.2g (2016) |
|           | (2016; 1.1.1 kept), .so.3 (Sep 2021;   |      | c: 3.0.4, 3.0.6, |
|           | 3.0 to 3.6 keep it)                    |      | 3.0.14, 3.5      |
| libpython | libpython3.X.so.1.0, new file/minor    | yes  | b: 3.9.3, 3.13.2 |
| readline  | .so.5 (2005), .so.6 (2009), .so.7      | yes  | c: 8.1 bracketed |
|           | (2016), .so.8 (8.0 2019; 8.1-8.3 keep) |      | paste (2020)     |
| libxml2   | libxml2.so.2 (1999 to 2.13),           | NO   | b: 2.9.10, 2.10, |
|           | libxml2.so.16 (2.14, Mar 2025)         | <214 | 2.11, 2.12, 2.13 |
| sqlite    | libsqlite3.so.0 since 3.0 (2004)       | yes  | c: planner perf  |
| bzip2     | libbz2.so.1.0 since 1.0 (2000)         | yes  | c: 1.0.7 (2019)  |
| xz        | liblzma.so.5 since 5.0.0 (2010)        | yes  | c: 5.6.0/1 door  |
| gmp       | .so.3 (4.x), .so.10 (5.0.0, Jan 2010,  | yes  | none (bdivmod    |
|           | kept through 6.3.0)                    |      | removal bumped)  |
| mpfr      | .so.1 (2.4), .so.4 (3.x, 2010), .so.6  | yes  | none found       |
|           | (4.0, Dec 2017, kept through 4.2)      |      |                  |
| mpc       | .so.2 (0.9), .so.3 (1.0, Jul 2012, on) | yes  | none found       |
| isl       | .so.10 (0.12), .so.13 (0.14), .so.15   | yes, | none; class d    |
|           | (0.15-18), .so.19 (0.19-20), .so.21,   | ofte | risk only        |
|           | .so.22 (0.22), .so.23 (0.23 to 0.28)   |      |                  |

Notes per row:

zlib. ABI Tracker: 100% or 99.4% for every release since 1.2.5.3
(2012); the only symbol removals were 12 symbols in 1.2.4.5 (2010,
pre-history for us). Not a single soname change ever. Breaks were
behavioural (section 2, items 1 to 3).

zstd. Upstream policy: "the stable section of the API only changes
when the second digit changes ... the experimental section can, and
does, evolve at each iteration". The experimental symbols
(ZSTD_STATIC_LINKING_ONLY) are exported from libzstd.so.1 and have
been removed or re-typed under that soname: ABI Tracker shows removed
exported symbols in 1.3.0 (5), 1.3.2 (3), 1.3.4 (3), 1.3.8 (6), 1.4.0
(2), 1.4.7 (1). Example, 1.3.7 to 1.3.8 (Dec 2018): removed
ZSTD_compress_generic, ZSTD_decompress_generic, ZSTD_CCtx_resetParameters,
ZSTD_setDStreamParameter, ...; ZSTD_CCtx_reset / ZSTD_DCtx_reset gained
a parameter and changed return type from void to size_t. Anything that
called those dynamically kept loading (lazy binding) and failed at the
first call or misbehaved. Stable-API users: 100% compatible every
release. Our GCC/binutils use only the stable API.

ncurses. ABI 6 (6.0, Aug 2015) was a deliberate bump: cchar_t widened
for more than 16 colours, mouse encoding changed for a 5th button.
libtinfo is a build-time split (--with-termlib) and follows the same
ABI number; Debian has shipped libtinfo separately since the 5.x era.
6.1 (Jan 2018) kept .so.6 and upstream states applications linked to
6.0 "should not require re-linking since the binary interface did not
change"; but 6.1 introduced a new terminfo on-disk format (magic 01036,
32-bit numbers) which tic writes by default when built with wide
support. ABI Tracker: 6.1 removed 2 symbols from libncurses.so.6.

libffi. Two bumps in two years (3.3: .so.7; 3.4: .so.8), both stated
by Fedora as "SONAME bump due to ABI changes". 3.4's release notes do
not say which ABI element changed; FFI_GO_CLOSURES was 3.3's "new API
in support of GO closures". Static trampolines (3.4.2) store their
handle in a union inside ffi_closure so the struct size is unchanged.
ABI Tracker: 100% within each soname. 3.5 to 3.8 (2025-2026) kept
.so.8.

libxcrypt. libcrypt.so.1 is the glibc-compatible ABI (with the
obsolete API, needed by binary-only applications); libcrypt.so.2 is
what you get with --disable-obsolete-api. Arch ships .so.2 plus a
libxcrypt-compat package for .so.1. 4.4.14 through 4.4.33: 100% every
release in ABI Tracker; 4.4.x through 4.5.2 (2025) kept .so.2.

elfutils. All libraries at soname 1 for the whole tracked range
(0.145 in 2010 to 0.182 in 2020), 100% backward compatible each time,
with symbol versions ELFUTILS_1.x added for new functions, so a binary
built against a newer elfutils fails cleanly on an older one (class a).

flex. libfl.so.2 only provides yywrap and a default main; no incident
found, almost nothing links it dynamically.

tcl. Per-minor soname (libtcl8.6.so); Tcl 9.0 (Sep 2024) is
libtcl9.0.so and extensions built for 8.6 are rejected by the stubs
mechanism ("interpreter uses an incompatible stubs mechanism"). Class
d only.

expat. libtool version-info current:revision:age is bumped every
release but age tracks current, so the file stays libexpat.so.1.N.M
and the soname libexpat.so.1. 100% in ABI Tracker for all of 2.x. All
breaks were behavioural (section 2).

openssl. Sonames follow the versioning policy since 1.0.0, with the
1.1.1 exception (kept .so.1.1 and is compatible; ABI Tracker 99.5%,
373 added symbols) and 3.x minors sharing .so.3 by design (each minor
adds an OPENSSL_3.N.0 symbol-version node, so new-on-old is class a
and reported at load: "version OPENSSL_3.2.0 not found"). Known
same-soname regressions are in section 2.

libpython. The "1.0" never changes; the minor is in the file name, so
3.12 to 3.13 is effectively a soname change (class d). Within a minor
the ABI is promised stable and has slipped twice (section 2).

readline. Bumps on every major (5, 6, 7, 8); ABI Tracker 100% within
each. 8.1 changed default behaviour (bracketed paste) with no ABI
change; 8.2 and 8.3 kept .so.8.

libxml2. The counter-example for the whole set. From 2.9.10 (2019) to
2.13 (2024) the project removed exported symbols and changed public
structs and function signatures while keeping libxml2.so.2; distros
masked or reverted (Gentoo masked 2.13 in July 2024; Debian shipped
"2.12.7+dfsg+really2.9.14"). 2.14.0 (Mar 2025) finally bumped to
libxml2.so.16 and states "binary compatibility is restricted to
versions 2.14 or newer". 2.15.0 (Sep 2025) removed the built-in HTTP
client and LZMA support again.

sqlite. libsqlite3.so.0 for 20+ years, 100% in ABI Tracker for every
release 2012-2023 (3.41.1: 99.8%, additions only). Behavioural drift is
in the query planner (performance regressions reported per release on
the SQLite forum) and in compile-time defaults chosen by the distro.

bzip2. libbz2.so.1.0 since 2000, three releases in 20 years. The one
regression (1.0.7) was behavioural and fixed in 1.0.8 within a month.

xz. liblzma.so.5 since 2010 with XZ_5.0 / 5.2 / 5.4 / 5.6 / 5.8 symbol
versions. 5.6.2 (May 2024) removed the backdoor and IFUNC support; the
exported ABI is the same as 5.6.0/5.6.1 (the backdoor added no
symbol, it hooked RSA_public_decrypt via IFUNC/audit at load).

gmp. libgmp.so.10 since 5.0.0 (Jan 2010). Every 5.x/6.x announcement:
"upwardly source and binary compatible with 6.2, 6.1, 6.0, 5.1, 5.0,
4.x, and 3.x" except users of the semi-documented mpn_bdivmod (whose
removal is what motivated the .so.3 to .so.10 jump). mp_bitcnt_t
(introduced 5.0) is a typedef of unsigned long, no ABI effect.

mpfr. libmpfr.so.4 for 3.x (2010-2017), .so.6 for 4.x (Dec 2017 on),
4.1 and 4.2 kept it; 4.0.0 notes one API+ABI change (mpfr_set_exp)
which is why the soname moved. MPFR's INSTALL warns binary distros to
run "make check-gmp-symbols" because an MPFR that uses GMP internals
"could yield failures in case of GMP upgrade without a MPFR rebuild";
this is the one dependency in the set whose ABI contract is with
another library's internals.

mpc. libmpc.so.3 since 1.0 (Jul 2012), 100% across 1.0.x, 1.1, 1.2,
1.3, 1.4 (Arch shipped 1.4.0 and 1.4.1 in 2026 under the same soname).

isl. Bumps the soname on almost every feature release (Debian binary
packages libisl10, libisl13, libisl15, libisl19, libisl21, libisl22,
libisl23). GCC's cc1 links it; a distro isl update is class d, visible.

## 2. Incident table

Fixed fields first, then one paragraph per row. "son?" = soname
changed with the release.

| #  | date     | library   | versions            | son? | class |
|----|----------|-----------|---------------------|------|-------|
| 1  | 2022-03  | zlib      | 1.2.11 -> 1.2.12    | no   | c     |
| 2  | 2022-08  | zlib      | 1.2.12 + CVE patch  | no   | c     |
| 3  | 2023-01  | zlib      | 1.2.13 (s390x)      | no   | c     |
| 4  | 2018-12  | zstd      | 1.3.7 -> 1.3.8      | no   | b     |
| 5  | 2019-04  | zstd      | 1.3.8 -> 1.4.0      | no   | b     |
| 6  | 2015-08  | ncurses   | 5.9 -> 6.0          | yes  | d     |
| 7  | 2018-01  | ncurses   | 6.0 -> 6.1          | no   | c*    |
| 8  | 2019-11  | libffi    | 3.2.1 -> 3.3        | yes  | d     |
| 9  | 2021-06  | libffi    | 3.3 -> 3.4          | yes  | d     |
| 10 | 2016-03  | openssl   | 1.0.2f -> 1.0.2g    | no   | b     |
| 11 | 2016-08  | openssl   | 1.0.2 -> 1.1.0      | yes  | d     |
| 12 | 2021-09  | openssl   | 1.1.1 -> 3.0.0      | yes  | d     |
| 13 | 2022-07  | openssl   | 3.0.4               | no   | c     |
| 14 | 2022-10  | openssl   | 3.0.6 / 1.1.1r      | no   | c     |
| 15 | 2024-06  | openssl   | 3.0.14 3.1.6 3.2.2  | no   | c     |
| 16 | 2025-04  | openssl   | 3.4 -> 3.5          | no   | c     |
| 17 | 2019-10  | libxml2   | 2.9.9 -> 2.9.10     | no   | b     |
| 18 | 2022-08  | libxml2   | 2.9.14 -> 2.10.0    | no   | b     |
| 19 | 2022-11  | libxml2   | 2.10.3 (Fedora)     | no   | b     |
| 20 | 2023-04  | libxml2   | 2.10 -> 2.11.0      | no   | b     |
| 21 | 2024-06  | libxml2   | 2.12 -> 2.13        | no   | b     |
| 22 | 2025-03  | libxml2   | 2.13 -> 2.14.0      | yes  | d     |
| 23 | 2021-04  | libpython | 3.9.2 -> 3.9.3      | no   | b     |
| 24 | 2025-02  | libpython | 3.13.1 -> 3.13.2    | no   | b     |
| 25 | 2020-12  | readline  | 8.0 -> 8.1          | no   | c     |
| 26 | 2016-09  | readline  | 6.3 -> 7.0          | yes  | d     |
| 27 | 2021-05  | expat     | 2.3.0 -> 2.4.0      | no   | c     |
| 28 | 2024-02  | expat     | 2.5.0 -> 2.6.0      | no   | c     |
| 29 | 2025-03  | expat     | 2.7.0 -> 2.7.1      | no   | c     |
| 30 | 2019-06  | bzip2     | 1.0.6 -> 1.0.7      | no   | c     |
| 31 | 2024-03  | xz        | 5.6.0 / 5.6.1       | no   | c     |
| 32 | 2016-20  | xz        | RHEL7 5.1.2alpha    | no   | a     |
| 33 | 2010-01  | gmp       | 4.3 -> 5.0.0        | yes  | d     |
| 34 | 2017-12  | mpfr      | 3.1 -> 4.0.0        | yes  | d     |
| 35 | 2024-09  | tcl       | 8.6 -> 9.0          | yes  | d     |
| 36 | various  | sqlite    | 3.44, 3.47, ...     | no   | c     |
| 37 | 2018-03  | isl       | 0.18 -> 0.19        | yes  | d     |

1. zlib 1.2.12 (27 Mar 2022) made crc32() and adler32() sensitive to
   bits set above the low 32 of the crc argument, which earlier
   releases had ignored. Java runtimes (OpenJDK/OpenJ9 passed
   sign-extended values) started failing with "invalid entry CRC" and
   "zip END header not found"; Spring Boot builds, NetBeans, JetBrains
   Toolbox, Minecraft Fabric Loader broke on Arch (FS#74371), Mageia
   reverted to the lenient behaviour (MGAA-2022-0052), IBM published
   an AIX advisory. Upstream commit ec3df00 (30 Mar 2022) "restores
   that behavior, so that applications with such bugs will continue
   to operate as before"; shipped in 1.2.13 (13 Oct 2022). Old binary
   on new libz.so.1, no ABI change.
2. zlib CVE-2022-37434 (Aug 2022): the first upstream fix for the
   inflateGetHeader() over-read (commit eff308a) itself regressed
   (NULL dereference, reported through curl) and needed a second
   commit (1eb7682); distros that shipped only the first patch under
   the unchanged 1.2.12 version string pushed a broken libz.so.1 and
   then a second update (Debian 1016710, Ubuntu CVE page). Same
   soname, same version, two different behaviours within weeks.
3. Ubuntu 2002511 (Jan 2023): the s390x DFLTCC acceleration patch
   carried by Ubuntu/Fedora for zlib 1.2.13 (and backported to 1.2.11)
   updated strm.adler for raw streams and made libxml2/lxml fail to
   parse gzipped XML ("Document is empty"). A distro patch, not
   upstream, but it is exactly the case of "same soname, same version
   string, different behaviour".
4. zstd 1.3.8 (27 Dec 2018) removed the exported experimental symbols
   ZSTD_compress_generic, ZSTD_compress_generic_simpleArgs,
   ZSTD_decompress_generic, ZSTD_decompress_generic_simpleArgs,
   ZSTD_CCtx_resetParameters, ZSTD_setDStreamParameter and changed the
   signature of ZSTD_CCtx_reset/ZSTD_DCtx_reset (added a parameter,
   void -> size_t). libzstd.so.1 unchanged. Anyone who linked the
   experimental API dynamically failed at first call (removed) or
   silently passed garbage (re-typed). Upstream position: experimental
   symbols carry no stability promise.
5. zstd 1.4.0 (16 Apr 2019) removed 2 more exported experimental
   symbols (ABI Tracker: 99%); 1.4.7 removed 1. Same shape as row 4.
6. ncurses 6.0 (8 Aug 2015): ABI 5 -> 6 for the wider cchar_t and the
   new mouse encoding; libncurses, libncursesw, libtinfo, libform,
   libmenu, libpanel all moved to .so.6. Textbook class d.
7. ncurses 6.1 (27 Jan 2018): same .so.6, but tic writes the new
   extended-number terminfo format (magic 01036, 32-bit numbers) for
   the -256color entries (pairs went from 32767 to 65536). Readers
   that are not ncurses 6.1+ could not parse them: unibilium (hence
   neovim), slang, Midnight Commander on Arch ("Unknown terminal:
   tmux-256color", bug-ncurses Jan 2018), wine on Arch because
   lib32-ncurses lagged (FS#57428), Julia's terminfo parser in 2023
   (#51110). The ncurses FAQ: "the required format change for extended
   numbers briefly broke unibilium (as well as slang, which does use
   terminfo)". Marked c* because the thing that changed is a data
   file on disk, not the library ABI: a prebuilt tool carrying its own
   older ncurses/terminfo reader but reading the host's
   /usr/share/terminfo is exposed, and no soname check sees it.
8. libffi 3.3 (23 Nov 2019): .so.6 -> .so.7; Fedora: "SONAME bump due
   to ABI changes", 38 packages rebuilt. Class d.
9. libffi 3.4 (28 Jun 2021): .so.7 -> .so.8, again "due to ABI
   changes"; Fedora shipped a libffi3.1 compat package instead of
   rebuilding. Class d. (Reason not spelled out; see section 6.)
10. openssl 1.0.2g and 1.0.1s (1 Mar 2016, DROWN fix): SSLv2 disabled
   at build time, removing 34 SSLv2 symbols from libssl while the
   soname stayed (Fedora libssl.so.10; RH bug 1313509). Qt5Network,
   Python's _ssl, curl and about 397 Fedora packages referenced them;
   Fedora revoked the update on day two. Upstream restored the three
   SSLv2_*method symbols as stubs returning NULL in 1.0.2h. Old binary
   on new lib, same soname: the loader catches the missing symbol
   (with lazy binding only at first call), a soname check does not.
11. openssl 1.1.0 (Aug 2016): .so.1.0.0 -> .so.1.1, 468 symbols
   removed, structs made opaque. Class d.
12. openssl 3.0.0 (Sep 2021): .so.1.1 -> .so.3, 4456 symbols removed
   (ABI Tracker). Class d. Also large behavioural changes (legacy
   provider, SHA-1 signatures rejected at default security level,
   "unsafe legacy renegotiation disabled") that hit even rebuilt
   applications; those are beyond the same-soname question.
13. openssl 3.0.4 (21 Jun 2022): heap corruption in the AVX-512 IFMA
   RSA path (CVE-2022-2274) introduced by a patch release; fixed in
   3.0.5 (5 Jul 2022). Same .so.3, pure behaviour.
14. openssl 3.0.6 and 1.1.1r (11 Oct 2022) were withdrawn the next
   day for "a significant regression" and replaced by 3.0.7/1.1.1s.
15. openssl 3.0.14 / 3.1.6 / 3.2.2 / 3.3.1 (Jun 2024, CVE-2024-5535):
   SSL_select_next_proto now rejects malformed or empty ALPN/NPN
   lists instead of returning memory past the buffer. OpenSSL's own
   QUIC test server broke because it "was using incorrectly formatted
   ALPN data, which went unnoticed with the previous implementation".
   Same soname; only callers that were already wrong are affected.
16. openssl 3.5.0 (Apr 2025): default TLS 1.3 key-share list changed to
   X25519MLKEM768 + X25519; every ClientHello now carries an ML-KEM
   key share. Same .so.3; interoperability behaviour changed for
   every linked program without recompilation.
17. libxml2 2.9.10 (30 Oct 2019): 21 exported symbols removed, soname
   kept (ABI Tracker 98.8%).
18. libxml2 2.10.0 (17 Aug 2022): 103 exported symbols removed (Docbook
   module, xmlunicode.h functions, legacy symbols), soname kept
   (93.8%). NEWS offers --with-legacy to get stubs back.
19. Fedora CoreOS #1336 (3 Nov 2022): the 2.10.3 update dropped 111
   symbols; open-vm-tools' vgauthd failed with "undefined symbol:
   xmlIOFTPRead". Whether the 111 came from upstream or from a Fedora
   build-option change is not settled in the thread (section 6).
20. libxml2 2.11.0 (28 Apr 2023): "finally fixes symbol visibility on
   UNIX systems. Internal symbols will now be hidden." Anything that
   had linked an internal symbol lost it. Soname kept.
21. libxml2 2.13 (12 Jun 2024): xmlSetTreeDoc/xmlSetListDoc changed
   return type void -> int, xmlParserCtxt grew errorHandler/errorCtxt
   fields, nanohttp removed (xmlNanoHTTP* gone). Gentoo 935452 (raptor
   "symbol lookup error: undefined symbol: xmlNanoHTTPReturnCode,
   version LIBXML2_2.4.30"), masked 2.13 for four months; Debian
   1073508 blocked chromium in trixie and titled the bug "just another
   API+ABI break; please bump soname". GNOME issue 751 measured 2.9.14
   vs 2.13.1 at 73.8% compatible, 70 removed symbols.
22. libxml2 2.14.0 (27 Mar 2025): soname libxml2.so.2 -> libxml2.so.16,
   "binary compatibility is restricted to versions 2.14 or newer".
   Class d, and the first time since 1999. Real-world effect on a
   prebuilt-binary user: LLVM issue 138225, "lld from 20.1.4 prebuilt
   release archive can't run on Arch Linux due to libxml2 ABI break".
   This is precisely what a soname check DOES catch.
23. Python 3.9.3 (2 Apr 2021): a bugfix changed the size of
   PyThreadState; C extensions built against 3.9.0-3.9.2 crashed on
   32-bit systems. 3.9.4 shipped ~24 hours later to "restore ABI
   compatibility". libpython3.9.so.1.0 unchanged.
24. Python 3.13.2 and 3.12.9 (Feb 2025): backport of
   PyConfig.use_system_logger changed the PyConfig layout; binaries
   using the PyConfig API built on 3.13.0/3.13.1 "would potentially
   misbehave" on the newer interpreter (gh-130940, found by PyO3's FFI
   check). Removed again from the 3.13 branch.
25. readline 8.1 (Dec 2020): "Bracketed paste mode is enabled by
   default". Every program linked against libreadline.so.8 started
   emitting the enable/disable escape sequences and treating a
   multi-line paste as one editable buffer: Python's REPL (bpo-42819),
   FRRouting vtysh (#8029), pexpect-driven scripts, Fedora users
   (RH 1968143, closed NOTABUG). Same soname, nothing to link.
26. readline 7.0 (Sep 2016): .so.6 -> .so.7 (99.64% in ABI Tracker, 16
   added). Class d, listed for completeness.
27. expat 2.4.0 (23 May 2021): billion-laughs protection on by default;
   documents that previously parsed can now fail with
   XML_ERROR_AMPLIFICATION_LIMIT_BREACH.
28. expat 2.6.0 (6 Feb 2024, CVE-2023-52425): "reparse deferral". With
   chunked input, "registered handlers may ... no longer be called
   right after pushing new input to the parser". Python's test suite
   broke and CPython had to expose XML_SetReparseDeferralEnabled and
   add flush() methods in five places (gh-115398, released in 3.12.x /
   3.13). libexpat.so.1 unchanged.
29. expat 2.7.1 (27 Mar 2025): "Restore event pointer behavior from
   Expat 2.6.4", i.e. 2.7.0 had changed observable behaviour and it
   was reverted within two weeks.
30. bzip2 1.0.7 (27 Jun 2019): the CVE-2019-12900 fix capped the
   number of selectors and libbz2 started rejecting valid files
   produced by lbzip2 with "Data integrity error" (Ubuntu 1834494,
   NVIDIA Jetson archives; Debian 931278). 1.0.8 (13 Jul 2019)
   "Accept as many selectors as the file format allows".
   libbz2.so.1.0 unchanged.
31. xz 5.6.0 / 5.6.1 (Feb-Mar 2024, CVE-2024-3094): liblzma.so.5 with
   the same exported ABI as 5.4.x plus one new function, but with a
   payload that hooked RSA_public_decrypt in sshd at load time. The
   strongest possible class c: identical soname, identical symbols,
   different behaviour. 5.6.2 (29 May 2024) removed it and IFUNC.
32. RHEL/CentOS 7 shipped liblzma 5.1.2alpha with an XZ_5.1.2alpha
   symbol-version node that upstream 5.2.x does not have; binaries
   built on RHEL 7 (rpm itself) fail on an upstream liblzma.so.5 with
   "version XZ_5.1.2alpha not found" (spack #6255, easybuild #4036).
   Upstream 5.2.5 (2020) added a compatibility node. Class a (loader
   reports it), same soname on both sides.
33. gmp 5.0.0 (Jan 2010): libgmp.so.3 -> .so.10 because the
   semi-documented mpn_bdivmod was removed. Class d, and the only ABI
   break GMP has had; 6.3.0 is still .so.10.
34. mpfr 4.0.0 (25 Dec 2017): .so.4 -> .so.6; "The behavior of the
   mpfr_set_exp function changed ... (this modifies both the API and
   the ABI)". Class d.
35. tcl 9.0.0 (Sep 2024): libtcl9.0.so; 8.6 extensions refused by the
   stubs check. Class d; homebrew users of pyenv/OpenROAD hit it as a
   missing libtcl8.6 file, exactly what a soname check reports.
36. sqlite: no ABI incident in 20 years. Behavioural: query-planner
   changes cause performance regressions reported on the SQLite forum
   for 3.44.0 (recursive CTE, workaround PRAGMA automatic_index=OFF),
   3.46.1 -> 3.47 (same schema and data, "only the executed code
   changed"), a 3.24.0 -> 3.41.2 case going from 2 minutes to over an
   hour. Correctness stayed; latency did not.
37. isl 0.19 (Mar 2018): Debian "Bump the libisl soname" (libisl15 ->
   libisl19); similar bumps at 0.14, 0.21, 0.22, 0.23. Class d every
   time.

## 3. Update frequency versus incidents

Source: archive.archlinux.org directory listings for the x86_64
package of each library (every build Arch pushed, including
pkgrel-only rebuilds). Counts are package pushes per calendar year;
roughly a third of them are rebuilds of the same upstream version
(pkgrel bump), the rest are upstream releases.

| package   | 2024 | 2025 | notes                                    |
|-----------|------|------|------------------------------------------|
| zlib      |  2   |  0   | 1.3.1 in 2024; nothing in 2025           |
| zstd      |  1   |  2   | 1.5.6; 1.5.7                             |
| openssl   |  4   |  5   | 3.2.x -> 3.3 -> 3.4 -> 3.5, same .so.3   |
| ncurses   |  5   |  1   | 6.4 snapshot rebuilds, 6.5               |
| libffi    |  2   |  5   | 3.4.5-3.4.8, 3.5.0-3.5.2, all .so.8      |
| libxcrypt |  2   |  4   | 4.4.36-4.5.2, all .so.2                  |
| elfutils  |  7   |  8   | 0.190-0.194; many pkgrel rebuilds        |
| flex      |  0   |  0   | 2.6.4 since 2017                         |
| tcl       |  5   |  1+  | 8.6.14, 8.6.16, then 9.0.1 (Jan 2025)    |
| expat     |  6   |  4   | 2.6.0-2.6.4; 2.7.0-2.7.3                 |
| python    | 12   | 14   | 3.11 -> 3.12 (Apr 2024) -> 3.13 (Nov)    |
| readline  |  2   |  6   | 8.2 patches; 8.3.0 (Jul 2025)            |
| libxml2   | 19   | 20   | 2.12.4 ... 2.13.5; 2.13.5 ... 2.15.1     |
| sqlite    |  5   | 10   |                                          |
| bzip2     |  1   |  0   | 1.0.8 since 2019                         |
| xz        |  7   |  4   | 5.4.6, 5.6.0/1 (x3), 5.6.2, 5.6.3; 5.8   |
| gmp       |  1   |  0   | 6.3.0 since Jul 2023                     |
| mpfr      |  3   |  3   | 4.2.1 rebuilds, 4.2.2                    |
| libmpc    |  1   |  0   |                                          |
| libisl    |  2   |  0   | 0.26 rebuild, 0.27                       |
| total     | 87   | 87   |                                          |

So a rolling distro pushes on the order of 85-90 updates per year for
this set of 20 libraries, i.e. one every 4 days; about 60 of them are
new upstream versions. Four packages (libxml2, python, elfutils,
sqlite) account for more than half.

Against those ~175 pushes in 2024-2025, the incidents in section 2
that fall in the window and would affect an already-built binary:

    row 15  openssl SSL_select_next_proto (c, only buggy callers)
    row 16  openssl 3.5 PQ defaults           (c, network behaviour)
    row 21  libxml2 2.13 removals             (b, real breakage)
    row 22  libxml2 2.14 soname               (d, caught)
    row 24  python 3.13.2 PyConfig            (b, PyConfig users only)
    row 28  expat 2.6.0 reparse deferral      (c, chunked parsers)
    row 29  expat 2.7.0/2.7.1 event pointer   (c, minor)
    row 31  xz 5.6.0/5.6.1                    (c, sshd only in practice)
    row 35  tcl 9.0                           (d, caught)

Nine events in two years, two of them caught by a soname check, five
class c, two class b. Per push that is about 5%; but the distribution
is extremely uneven: 15 of the 20 libraries had zero same-soname
incidents in the two years (zlib, zstd, ncurses, libffi, libxcrypt,
elfutils, flex, readline, sqlite, bzip2, gmp, mpfr, mpc, isl, and tcl
whose only event was a visible soname change); the seven same-soname
events sit in openssl, libxml2, libpython, expat and xz. Over the
longer window (2015-2025) the picture is the same: zlib had one bad
release in 25 years, bzip2 one in 20, readline one behaviour change in
15, expat two or three, openssl one same-soname symbol removal in 2016
plus behavioural patch releases, libpython two slips, and libxml2 a
break in essentially every feature release from 2019 to 2024.

## 4. What this means for a "sonames resolve" check

What the check sees. Class d, always: every deliberate ABI bump in
this set (ncurses 6, libffi 7 and 8, openssl 1.1 and 3, readline 7 and
8, gmp 10, mpfr 6, mpc 3, tcl 9.0, isl every other release, libxml2 16,
libpython per minor) changes the file name the loader looks for, and
the LLVM/lld-on-Arch and OpenROAD/pyenv-on-homebrew reports show that
a prebuilt binary really does die of exactly this. For 16 of the 20
libraries, class d is the only kind of break that has happened in the
period we care about, so for them the check is not just necessary but
in practice sufficient.

What the check is blind to, with the evidence:

1. Symbol removal under an unchanged soname (class b, removed symbol).
   libxml2 five times in five years, zstd's experimental symbols six
   times, openssl once (2016). The dynamic loader detects it, but only
   at load with BIND_NOW, otherwise at first call, so "the binary
   starts" is not proof either. A cheap complement closes this gap:
   for each of our binaries, record the set of undefined versioned
   symbols it imports (readelf --dyn-syms, or ldd -r / LD_BIND_NOW=1
   as a smoke run) and re-check that each still resolves in the
   library the soname now points at. This is what the libxml2 2.13 and
   openssl 1.0.2g victims would have needed.
2. Struct-layout or signature change under an unchanged soname (class
   b, silent). libxml2 2.12/2.13 (xmlParserCtxt fields, void->int
   returns), zstd 1.3.8 (ZSTD_CCtx_reset), Python 3.9.3 and 3.13.2.
   Nothing at load time sees this; the failure is a crash or wrong
   answer later. The only defences are (i) knowing which libraries
   have a record of it (libxml2, libpython, zstd-experimental) and
   treating any version change of those as "rebuild", and (ii) running
   the binary end to end.
3. Behaviour change with no ABI change (class c). zlib 1.2.12, bzip2
   1.0.7, readline 8.1, expat 2.4.0/2.6.0/2.7.0, openssl 3.0.4/3.0.6/
   3.0.14/3.5, xz 5.6, sqlite planner. Undetectable by any static
   inspection of the binary or the library; the only test is to run
   the tool on real input. For our tree that means the same thing the
   package-manager rules already demand for a changed package: boot
   the result and exercise it.
4. Data-format changes behind the library (ncurses 6.1 terminfo). A
   prebuilt tic/infocmp or anything with its own terminfo reader that
   consults the host's /usr/share/terminfo can fail even when the
   libraries it links are untouched. Relevant if our ncurses tools are
   older than the host's terminfo database.

A practical grading of the 20 libraries by what a soname check buys:

    soname check alone has been sufficient (no same-soname break on
    record in 10+ years):
        libffi, libxcrypt, elfutils, flex, tcl, gmp, mpfr, mpc, isl,
        sqlite (ABI), readline (ABI), ncurses (ABI)
    soname check + imported-symbol check sufficient on record:
        zstd (only if we import experimental symbols; we do not),
        openssl 1.0.x era (removed symbols), libxml2 removals
    nothing short of running the binary catches the recorded breaks:
        zlib (1.2.12), bzip2 (1.0.7), expat (2.6.0), readline (8.1),
        openssl 3.x patch behaviour, xz 5.6, libpython patch slips,
        libxml2 struct changes, ncurses terminfo format

Two design conclusions follow. First, the soname check should stay:
it is cheap, it catches the most common deliberate break, and every
class d incident above would have been reported before the first run.
Second, it must not be presented as "the binary still works". The
recorded same-soname breaks cluster in four libraries (libxml2,
libpython, expat, openssl) plus one-off events elsewhere; a stronger
gate for those four (rebuild on any version change, or at least the
imported-symbol check plus a smoke run) covers most of the residual
risk at little cost, and a general "record the library's build-id or
package version at build time and re-run the smoke test when it
changes" covers the rest, including class c, which nothing else does.

## 5. Sources

ABI Tracker timelines (abi-laboratory.pro, view=timeline&l=NAME):
zlib (to 1.2.11, 2020), zstd (to 1.5.4, Mar 2023), ncurses (to 6.4,
Mar 2023), libffi (to 3.4.2, Feb 2022), openssl (to 3.0.1, Dec 2021),
libxml2 (to 2.10.3, Mar 2023), sqlite (to 3.41.1, Mar 2023), readline
(to 8.1, 2020), gmp (to 6.2.1, 2020), mpfr (to 4.2.0, Mar 2023), expat
(to 2.2.9, 2020), elfutils (to 0.182, 2020), xz (to 5.2.5, 2020), tcl
(to 9.0a1, 2020), mpc (to 1.1.0, 2020), libxcrypt (to 4.4.33, Dec
2022). Compat report zstd 1.3.7 vs 1.3.8:
abi-laboratory.pro index.php?view=compat_report&l=zstd&v1=1.3.7
&v2=1.3.8&obj=84e27&kind=abi (one URL, wrapped here)

zlib
- https://www.zlib.net/ChangeLog.txt
- https://github.com/madler/zlib/commit/ec3df00224d4b396e2ac6586ab5d25f673caa4c2
- https://bugs.archlinux.org/task/74371
- https://advisories.mageia.org/MGAA-2022-0052.html
- https://ubuntu.com/security/CVE-2022-37434
- https://groups.google.com/g/linux.debian.bugs.dist/c/oe7mAVSKmSA
- https://github.com/madler/zlib/issues/613 and /issues/634
- https://www.ibm.com/support/pages/node/6827629
- https://bugs.launchpad.net/bugs/2002511
- https://github.com/madler/zlib/issues/642 (see section 6)
- https://github.com/madler/zlib/issues/447 (see section 6)

zstd
- https://github.com/facebook/zstd/releases/tag/v1.4.0
- https://github.com/facebook/zstd/releases/tag/v1.5.0

ncurses
- https://invisible-island.net/ncurses/announce-6.0.html
- https://invisible-island.net/ncurses/announce-6.1.html
- https://invisible-island.net/ncurses/ncurses.faq.html
- https://lists.gnu.org/archive/html/bug-ncurses/2018-01/msg00052.html
- https://github.com/mauke/unibilium/issues/30
- https://bugs.archlinux.org/task/57428
- https://github.com/JuliaLang/julia/issues/51110

libffi
- https://github.com/libffi/libffi/releases
- https://raw.githubusercontent.com/libffi/libffi/v3.4.2/README.md
- https://fedoraproject.org/wiki/Changes/LIBFFI33
- https://fedoraproject.org/wiki/Changes/LIBFFI34
- https://github.com/libffi/libffi/pull/624 (static trampolines)

libxcrypt
- https://github.com/besser82/libxcrypt/blob/develop/README.md

openssl
- https://www.openssl-library.org/policies/general/versioning-policy/
- https://bugzilla.redhat.com/show_bug.cgi?id=1313509
- https://github.com/curl/curl/issues/299
- https://mta.openssl.org/pipermail/openssl-announce/2022-October/000237.html
- https://github.com/openssl/openssl/issues/18625
- https://github.com/openssl/openssl/pull/24717 and /pull/24718
- https://github.com/advisories/GHSA-4fc7-mvrr-wv2c
- https://bbs.archlinux.org/viewtopic.php?id=292458 (version node a)

libxml2
- https://gitlab.gnome.org/GNOME/libxml2/-/raw/master/NEWS
- https://gitlab.gnome.org/GNOME/libxml2/-/issues/751
- https://bugs.gentoo.org/935452
- https://bugs.debian.org/1073508
- https://github.com/coreos/fedora-coreos-tracker/issues/1336
- https://github.com/llvm/llvm-project/issues/138225
- https://github.com/conda-forge/libxml2-feedstock/issues/145
- https://packages.debian.org/changelog:libxml2

libpython
- https://discuss.python.org/t/8043 ("Python 3.9.3 contains an
  unintentional ABI incompatibility on 32-bit systems")
- https://github.com/python/cpython/issues/130940
- https://github.com/python/cpython/issues/83780 (see section 6)
- https://docs.python.org/3/c-api/stable.html

readline
- https://tiswww.case.edu/php/chet/readline/CHANGES
- https://lwn.net/Articles/839213/
- https://bugs.python.org/issue42819
- https://github.com/FRRouting/frr/issues/8029
- https://bugzilla.redhat.com/show_bug.cgi?id=1968143

expat
- https://raw.githubusercontent.com/libexpat/libexpat/R_2_6_0/expat/Changes
- https://github.com/libexpat/libexpat/blob/master/expat/Changes
- https://github.com/python/cpython/issues/115398

sqlite
- https://www.sqlite.org/queryplanner-ng.html
- https://sqlite.org/forum/forumpost/b21c2101a559be0a
- https://sqlite.org/forum/forumpost/1e25669426
- https://sqlite.org/forum/forumpost/10403068b1

bzip2
- https://bugs.launchpad.net/ubuntu/+source/bzip2/+bug/1834494
- https://sourceware.org/legacy-ml/bzip2-devel/2019-q2/msg00034.html

xz
- https://github.com/tukaani-project/xz/releases/tag/v5.6.2
- xz commit 913ddc5572b9455fa0cf299be2e35c708840e922 ("liblzma:
  Vaccinate against an ill patch from RHEL/CentOS 7")
- https://github.com/spack/spack/issues/6255
- https://gist.github.com/thesamesam/223949d5a074ebc3dce9ee78baad9e27

gmp / mpfr / mpc / isl
- https://gmplib.org/gmp5.0 , https://gmplib.org/gmp6.0 ,
  https://gmplib.org/gmp6.3
- https://gmplib.org/list-archives/gmp-announce/2010-January/000024.html
- https://www.mpfr.org/mpfr-4.0.0/
- https://fossies.org/linux/mpfr/INSTALL (check-gmp-symbols)
- https://launchpad.net/ubuntu/+source/isl/+changelog
- https://packages.debian.org/search?keywords=libisl
- https://packages.debian.org/buster/libisl19
- https://packages.ubuntu.com/focal/libisl22

tcl
- https://www.tcl-lang.org/software/tcltk/9.0.html
- https://core.tcl-lang.org/tips/doc/trunk/tip/628.md
- https://github.com/pyenv/pyenv/issues/3116

Update frequency (Arch Linux Archive directory listings)
- https://archive.archlinux.org/packages/<x>/<pkg>/ for zlib, zstd,
  openssl, ncurses, libffi, libxcrypt, elfutils, flex, tcl, expat,
  python, readline, libxml2, sqlite, bzip2, xz, gmp, mpfr, libmpc,
  libisl (fetched 2026-09-17).

## 6. Unverified or not confirmed

- zlib 1.2.12 "inflate / Z_NEED_DICT behaviour change": not found. The
  node.js commit "zlib: fix raw inflate with custom dictionary" is a
  fix of node's own call sequence (inflateSetDictionary right after
  inflateInit2 for raw streams), not a zlib change. The 1.2.12
  ChangeLog only has "Don't compute check value for raw inflate if
  asked to validate". Treated as no incident.
- zlib 1.3 "zlib.h LARGEFILE64 changes": only header-level issues
  found (madler/zlib #447 on _LARGEFILE64_SOURCE vs _FILE_OFFSET_BITS,
  #674 on the _LARGEFILE64_SOURCE test, openj9 #1273 on HAVE_UNISTD_H
  arithmetic in 1.3.1). Build-time, no ABI effect on existing
  binaries. 1.3.1.2 (Dec 2025) "Make z_off_t 64 bits by default" is a
  header default that changes which gz* prototypes a rebuild sees;
  worth watching when we rebuild, irrelevant to already-built code.
- zlib 1.2.12 "missing ZLIB_1.2.x version symbols" (madler/zlib #642):
  a locally built 1.2.12 without the version script; reads as a
  build-configuration problem, not an upstream change. Not counted.
- zstd "ZSTD_e_* renames": these are enum values (ZSTD_EndDirective),
  source-level only; no runtime incident found. What did change at
  runtime were the exported experimental functions (rows 4 and 5).
- zstd 1.3.x soname: ABI Tracker reports "libzstd.so.1.3.7 ->
  libzstd.so.1.3.8", which looks like a CMake-build artefact (1.4.0's
  notes: "CMake now creates the libzstd.so.1 symlink"); the Makefile
  build and all distros used libzstd.so.1 throughout.
- ncurses NCURSES_OPAQUE / TERMINAL made opaque in 6.1: a compile-time
  matter; the 6.1 announcement says 6.0 binaries need no relink. The
  two symbols ABI Tracker shows removed from libncurses.so.6 in 6.1
  were not identified by name.
- libffi 3.4 soname bump: Fedora says "ABI changes" but neither the
  release notes nor the change page name the changed element; the
  FFI_GO_CLOSURES connection is inferred from 3.3's notes ("New API in
  support of GO closures") and remains unconfirmed for 3.4.
- gmp mp_bitcnt_t: introduced in 5.0 as unsigned long ("on some
  systems it will be an unsigned long long in the future"); no ABI
  change has been made to it since. Nothing to report.
- ABI Tracker rows that look like tracker artefacts rather than real
  events: mpfr 4.1.1 at 69.9% with 0 symbols changed; gmp 5.0.0 listed
  with soname 3 (upstream announced .so.10 for 5.0.0).
- Python 3.7.4 -> 3.7.5 PyGC_Head (bpo-39599): closed "not a bug" by
  the core team; not counted as an incident.
- libxml2 2.10.3 on Fedora CoreOS (row 19): whether the 111 symbols
  disappeared upstream or via a Fedora build-option change was not
  settled in the thread.
- isl soname mapping for versions before 0.19 (libisl10 for 0.12,
  libisl13 for 0.14, libisl15 for 0.15-0.18) comes from Debian binary
  package names and memory of GCC prerequisites; 0.19+ is confirmed by
  the Ubuntu changelog and Debian/Ubuntu package pages.
- Arch tcl count for 2025 may be truncated (listing ended at 9.0.1-1,
  Jan 2025); Tcl 9.0.2/9.0.3 were released later in 2025.
- openssl 3.x "3.5.5 x25519-mlkem fails with default provider"
  (openssl-users thread) was seen but not read in detail; not counted.
