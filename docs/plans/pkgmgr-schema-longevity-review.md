# pkgmgr schema longevity review

A design review of the `toolchain5/` on-disk schema, the cache, and
the package manager's core abstractions against six ten-year
scenarios. Read from the code on branch `exp-work`, with no prior
context; every claim cites a file and line. Nothing was modified.

Vocabulary used below for the verdicts:

  * HOLDS: the scenario needs no new directory level, no new
    coordinate, and no change to what a coordinate means.
  * HOLDS WITH VALUE-LEVEL EXTENSION: the scenario is absorbed by a
    new VALUE of an existing coordinate (a new machine, env or stack
    spelling), possibly with code that today assumes fewer values.
  * BREAKS: the scenario needs a fourth coordinate, a change to what
    a coordinate means, or a rewrite of a core abstraction.

## 1. Executive verdict

  1. Every package on linux-aarch64, darwin-aarch64, other Linux host
     arches, shared/copied trees: HOLDS WITH VALUE-LEVEL EXTENSION.
     The schema already spells hosts as `<os>-<arch>` values. The code
     hardcodes x86_64 glibc in about ten places, keys the distro tier
     on a rolling version string, and reads the host from globals
     rather than from the Scope. Sharing a tree across hosts works
     only at one absolute path and without concurrency.
  2. Five more target arches, several boards each: HOLDS. One caveat
     is real today (an arch with no boards gets env `any`, which the
     schema reserves for "self-contained").
  3. ~1000 target + ~1000 host packages, deep graphs, many versions,
     many stacks: schema HOLDS; the code BREAKS on a hard cap
     (`VersionSolver::MAX_NAMES = 1_000`) and degrades quadratically
     in the number of installs.
  4. A small Linux distro on the same manager: HOLDS WITH VALUE-LEVEL
     EXTENSION for the tree; the `<machine>` value must not be spelled
     `linux-<arch>` because that string means "the build host". The
     Tilck product layer is spread over eight files and must be
     factored into one owner first.
  5. "mylang" dependency/build manager: the pure core (~3,500 lines)
     extracts as is; `Package`/`Planner`/`PackageManager` are glued
     to Tilck; the version solver is pin-or-default only and BREAKS
     for range constraints.
  6. The cache: HOLDS for naming, resumability, upstream renames and
     growth; BREAKS on integrity (no checksum of anything, ever) and
     on concurrency (one shared `cache/tmp`, no lock).

The one item to fix before anything else is built on it: the
`<stack>` value is documented as free-form (`gcc-14.4.0-lto`) but
every reader in the code requires `gcc-<parsable Version>`. That is
the extension the ten-year rule leans on, and it does not work today
(section 6, item A).

## 2. The schema as it is

### 2.1 The three coordinates

`Coords` (`scripts/pkgmgr/coords.rb:33-140`) is the one class that
turns three strings into a path:

    <machine>/<env>/<stack>/{ sysroot/, pkgs/<pkg>/<ver>/ }

`root` (coords.rb:120), `pkgs_dir` (:125), `pkg_dir` (:128-130),
`sysroot` (:133) and `env_dir` (:136-139) are the only path builders.
Blank strings are refused (:56-60); `nil` becomes `any` (:48-54);
`any` is reserved in all three (:28-29, :35).

Values in use today, from `--print-layout` and `find toolchain5`:

  * machine: `linux-x86_64`, `noarch`, `tilck-i386`, `tilck-x86_64`,
    `tilck-riscv64`, `tilck-aarch64`.
  * env: `any`, `omarchy-4.0.2`, `omarchy-4.0.3`, `pc`, `qemu-virt`,
    `licheerv-nano`.
  * stack: `any`, `gcc-11.5.0` .. `gcc-16.2.0` (ours, under
    `linux-x86_64/any`), `gcc-16.2.1` (the system compiler, under
    `linux-x86_64/omarchy-4.0.3`), `gcc-13.3.0` (the cross compiler,
    under every `tilck-*`).

Who produces each value:

  * machine: `HOST_OS_ARCH` = `"#{HOST_OS}-#{HOST_ARCH.name}"`
    (`early_logic.rb:393`); `HOST_OS` is one of linux/macos/freebsd or
    exit (:206-215); `HOST_ARCH` must be in `ALL_HOST_ARCHS`
    (:176-194, `arch.rb:98-101`). Targets: `"tilck-#{a.name}"` spelled
    at `package.rb:604`, `layout.rb:57`, `main.rb:362`,
    `package_manager.rb:297`, `planner.rb:436,465`, `package.rb:2096,
    2101`, parsed back at `world.rb:165-166`.
  * env: `HOST_DISTRO` = `ID-VERSION_ID` from `/etc/os-release`
    (`early_logic.rb:239-249`), `macos-<major.minor>` (:251-259),
    `freebsd-<uname -r>` (:260-266). Targets: the board, from
    `Scope#board_of` (`scope.rb:39-43`) via `Package#target_board`
    (`package.rb:620`); an arch with no boards yields `nil`, hence
    `any`.
  * stack: `HOST_CC` = `<family>-<full version>` of `$CC`
    (`early_logic.rb:324-356`); ours: `Coords.stack_name(ver)` =
    `"gcc-#{ver}"` (`coords.rb:78`) through `pkgmgr.stack_coords`
    (`package_manager.rb:870-881`); targets: `"gcc-#{a.gcc_ver}"`
    spelled by hand at `package.rb:604`, `layout.rb:58`, `main.rb:362`,
    `package_manager.rb:298`, `planner.rb:493`.

The tier table that picks host coordinates is a closed `case` on four
symbols (`package.rb:563-599`): `:portable` -> (any, any), `:distro`
-> (DISTRO, any), `:compiler` -> (DISTRO, HOST_CC), `:stack` ->
`stack_coords`. There is no `else`; an unknown tier returns `nil`
coordinates silently.

### 2.2 Invariants that keep the three from ambiguity

  * A package name appears only under `pkgs/` (verified: every
    `Coords#pkg_dir` caller goes through `pkgs_dir`; `World.orphans_of`
    walks exactly `<m>/<e>/<s>/pkgs/<pkg>/<ver>` at `world.rb:88-121`).
  * `sysroot/` sits beside `pkgs/`, never inside (`coords.rb:122-133`).
  * The version level must parse as a `Version`
    (`world.rb:136-143`; grammar at `version.rb:108-120`).
  * Identity of an install is `(name, ver, Coords)`: `World#find`
    (`world.rb:78-82`), `InstallSelector#matches?`
    (`install_selector.rb:58-62`), `Package#find_install`
    (`package.rb:1853`). The lint forbids comparing `.arch ==` or
    `.compiler ==` outside the selector (per docs/package_manager.md
    "Correctness guarantees").
  * Paths are never parsed back into meaning, with three exceptions
    that are all in the schema's own owners: `world.rb:165-166`
    (`tilck-` prefix to an Architecture), `coords.rb:86-89`
    (`stack_ver`, `gcc-` prefix), and `package.rb:2080-2084,2098`
    (`stack_dirs_of`, `gcc-` prefix again, plus a second
    `sub("gcc-", "")`). `pkg_dirname` strips `host_` from a package
    NAME, not a path (`package.rb:1562`), and the name-side prefix is
    asserted at construction (`package.rb:447`).
  * Records live inside the version directory and carry no
    coordinates: `.install_origin` (`package.rb:57-78`, two words, no
    format line), `.built_against` (`package.rb:96-110`, `name ver`
    lines, no format line), `.build_inputs` (`build_inputs.rb:74-85`,
    has `format N`, absolute paths normalised to `$TC`/`$SRC`/`$HOME`
    at :56-64).

### 2.3 What the schema does NOT say, and code decides instead

  * Which stack a `host_gcc` install DEFINES. It is a `:distro`
    package (`host_gcc.rb:104`) at `<distro>/any/pkgs/gcc/<ver>`; the
    stack `linux-x86_64/any/gcc-<ver>` is associated with it only by
    `stack_gcc_ver` (:166) and `stack_of_install` (:173), i.e. by the
    version number. The path does not record it.
  * Which host built a `tilck-*` package that has host requirements
    (`licheerv_nano_boot.rb:55-58`: x86_64 Linux only). Not recorded.
  * Which system compiler built a `:distro` package. Not recorded
    (only `:compiler`-tier installs carry `HOST_CC` in the path).

## 3. Per-scenario analysis

### 3.1 Scenario 1: other hosts, shared trees

Hard cases and where they land:

  * Host package on linux-aarch64, host world included. Machine value
    `linux-aarch64` is produced by `HOST_OS_ARCH` unchanged. Two
    obstacles, both value-level: `ALL_HOST_ARCHS` (`arch.rb:98-101`)
    is a subset of Tilck's target `ALL_ARCHS`, so a host arch must
    exist as a Tilck `Architecture` with `elf_name`, `gcc_tc`, etc.
    (a ppc64le host needs a ppc64le Tilck arch entry); and the host
    world is x86_64 glibc by constant: `portability.rb:117`
    (`EM_X86_64`), `:169-172` (`SYSTEM_LIBDIRS`), `host_gcc.rb:272`
    (`LOADER`), `:430` (`SYSTEM_LOADER`), `glibc.rb:88`
    (`ld-linux-x86-64.so.2` in `expected_files`),
    `package_manager.rb:934` (audit loader path). The roots declare
    `host_arch_list: ["x86_64"]` (`host_gcc.rb:150-151`,
    `qemu.rb:136-137`) and the docs say so
    (`docs/package_manager.md:236`). Verdict: HOLDS WITH VALUE-LEVEL
    EXTENSION; the work is a `HostABI` value (e_machine, loader path,
    system lib dirs) selected by `HOST_OS_ARCH`.
  * A host package built by a stack whose compiler is itself a cross
    compiler (build on x86_64, run on aarch64). Placement is by
    runs-on and built-by (`toolchain5.md:150-151`): machine
    `linux-aarch64`, stack `gcc-14.4.0`. Nothing in the schema
    objects. The code has no notion of "build machine != run
    machine" for host packages (`Package#coords` reads
    `HOST_OS_ARCH` for the machine, `package.rb:566-587`), so it
    cannot be asked for today, but no level is missing. HOLDS as a
    value; not implemented.
  * darwin-aarch64. `HOST_OS` is `macos` (`early_logic.rb:209`), env
    `macos-26.4` (:251-259), `HOST_CC` `clang-17.0.0` (:281-282).
    `Coords#stack_ver` returns `nil` for `clang-*` (`coords.rb:87`),
    which is correct for a `:compiler`-tier coordinate. No host world
    on Darwin by design (needs Linux headers and glibc); the roots
    refuse (`host_gcc.rb:150`). Homebrew-dependent packages would be
    `macos-26.4-brew` per the design doc. HOLDS.
  * Windows. `get_host_os` exits (`early_logic.rb:212-214`). A
    `windows-x86_64` machine value is legal in the schema; the bash
    bootstrap, the symlink-farm sysroot and the ELF audit are not.
    Not a schema question; WSL is `linux-x86_64`. Recommend: declare
    out of scope in the docs.
  * A tree shared between two hosts. Each host has its own
    `<machine>` subtree and `World.machine_to_arch` scans only
    `HOST_OS_ARCH` (`world.rb:174`), so the other host's installs are
    invisible: not listed, not cleaned by `--clean`, not counted by
    `--autoremove`. Coexistence: yes. Management from one side: no.
    Two hosts running pkgmgr at once share `cache/tmp`
    (`cache.rb:372-379`, `:459-466`: the second run deletes the
    first's extraction) and `cache/partial/<file>`
    (`cache.rb:167-171`); no lock exists (no `flock` anywhere in
    `scripts/pkgmgr`). `staging/<pkg>/<ver>` is also shared across
    machines (`package.rb:673-675`: no machine in the staging path),
    so two hosts building the same package version collide.
  * A tree COPIED to another path. Every sysroot symlink is absolute
    (`sysroot.rb:144`, verified: `sysroot/usr/bin/gcc ->
    /home/vlad/dev/tilck/toolchain5/...`), every stack binary carries
    an absolute RPATH and interpreter (`host_gcc.rb:406-411`), and
    the audit allows exactly `[TC]` (`package_manager.rb:958`). The
    host world works only at the path it was built at. Source
    tarballs and target packages copy fine. This is by design
    (`other/toolchain_conf` comments) but is not written down as a
    sharing rule.
  * The distro env on a rolling host. `HOST_DISTRO` is
    `ID-VERSION_ID`. On this machine that is `omarchy-4.0.3`, and the
    tree already holds `omarchy-4.0.2/any/pkgs/ruby/` from before the
    last upgrade (empty, orphaned). Every `:distro` and `:compiler`
    install moves out of view at each distro point release: all six
    `host_gcc` (the roots of the six stacks), binutils, ruby, python,
    meson, ninja, mtools, mconf, ncurses, gtest. The stacks then read
    as not built (`show_stacks`, `package_manager.rb:906-919` finds
    the stack dirs but `world.of("host_gcc")` finds no compiler), and
    each stack's `sysroot/usr/bin/gcc` link dangles. Also
    `early_logic.rb:245-248` exits when `VERSION_ID` is absent, which
    is the case on vanilla Arch (`ID=arch`, no `VERSION_ID`; not
    verified on a real Arch box, but the bootstrap lists Arch as
    supported at `install_pkgs:258`). `HOST_CC` carries the full
    patch version (`gcc-16.2.1`), so `gtest` is orphaned by every
    distro compiler patch release. Verdict: the VALUE is wrong, not
    the level. See item B.

### 3.2 Scenario 2: five more target arches, several boards each

  * A new arch is an `Architecture` entry (`arch.rb:57-96`), a
    `other/gcc_tc_conf/<arch>/{min_ver,default_ver}` pair
    (`main.rb:114-120`), `other/bsp/<arch>/<board>` directories
    (`early_logic.rb:415-418`, `main.rb:439-443`), a cross-compiler
    package `gcc-<arch>-musl` registered in a loop (`gcc.rb:158-160`)
    and one `TilckStackPackage` per (arch, board)
    (`tilck_stack.rb:68-72`). All of that is values. HOLDS.
  * Two boards of one arch sharing a package: they do not share;
    each board is its own tree (`package.rb:620`, `scope.rb:39-43`),
    and a package built identically for two boards is built twice.
    That is the documented trade (`docs/package_manager.md`, "Two
    boards of one architecture are two separate trees"). HOLDS; the
    cost is build time, not ambiguity.
  * A libc or ABI variant of a target (musl vs glibc, hard vs soft
    float, ilp32 vs lp64). The design assigns these to `<stack>`
    (`toolchain5.md:146-148`: `gcc-13.3.0-musl`). The board is "what
    the machine provides"; an ABI is a property of the build
    environment, so the stack is the right coordinate. But the code
    cannot read such a stack: `stack_dirs_of` accepts the directory
    (`package.rb:2083`), then `SafeVer("13.3.0-musl")` is `nil` (the
    grammar at `version.rb:108-120` knows alpha/beta/rc suffixes
    only) and the install is skipped at `package.rb:2098-2099`; it
    reappears as an unclaimed "found" through the orphan walk. On the
    host side `host_stacks` (`package_manager.rb:906-919`) filter_maps
    on `stack_ver`, so a `gcc-14.4.0-lto` stack is never composed,
    listed or counted. HOLDS WITH VALUE-LEVEL EXTENSION in the
    schema; BREAKS in the code today. Item A.
  * An arch with no boards. `aarch64` has none (`arch.rb:88-96`), so
    `board_of` returns `nil` and the env becomes `any`
    (`layout.rb:57`, `package.rb:604` via `target_board`). The tree
    holds `tilck-aarch64/any/gcc-13.3.0/pkgs/` (empty), created on
    every run by `create_toolchain_dirs` (`main.rb:445-449`) for every
    arch. This contradicts `arch.rb:20-23` ("every arch has at least
    one board, because the board is part of the path") and gives
    `any` a second meaning ("no board yet") in the target namespace.
    `Package#scope_at` maps it back to `nil` (`package.rb:1099-1108`).
    Item K.
  * A machine-independent but env-dependent artifact (a board's
    device tree, a BSP blob). `Coords.new("noarch", "licheerv-nano",
    nil)` is legal, but `Package#coords` pins every noarch package to
    `noarch/any/any` (`package.rb:566`) and `noarch_package_get_
    install_list` reads only that. Value-level: let a noarch package
    with a `board_list` take the board as env. Not needed today.

### 3.3 Scenario 3: scale

Schema: `pkgs/<pkg>/<ver>` is flat per coordinates, versions are
siblings, stacks are siblings; six stacks of 45 packages plus six
QEMUs exist now and the listing handles them (`-l` runs in 0.24 s
with 350 install directories; measured). HOLDS.

Code, in order of severity:

  * `VersionSolver::MAX_NAMES = 1_000` (`version_solver.rb:60`,
    raised at :140-142) caps the number of distinct names in ONE
    resolution. `-s ALL`, a Tilck stack meta-package, or any request
    whose closure exceeds 1000 names raises `NonTerminatingWalk`. At
    2000 registered packages this is reached by construction. The
    dep resolver bounds its walks by the edge count instead
    (`dep_resolver.rb:108-110`), which is the right shape. BREAKS;
    trivial to fix now.
  * `World#of` and `World#find` are linear scans (`world.rb:58,
    78-82`). `Planner.install_graph` (`planner.rb:721-742`) calls
    `needs_of` per install, which calls `deps_of_install` which
    calls `world.of(d.name)` per dependency (:665-683), and
    `coords_of_install_for` per dependency version. That is
    O(I * D * I). `show_status_all` builds `states` with
    `judged.installs.find` per install (`package_manager.rb:423`),
    O(I^2). At 3,500 installs the quadratic parts are 100x today's;
    expect `-l`, `--autoremove` and `--check-for-updates` (which
    CMake runs on every configure, `CMakeLists.txt:1001`) in the
    seconds-to-tens-of-seconds range. Degrades; fix is an index
    (`Hash[name] -> installs`) on `World`.
  * `Planner.graph` (`planner.rb:55-60`) is rebuilt from scratch by
    every `dep_closure` call (`package_manager.rb:1132-1134` ->
    `build_dep_graph` :796 -> `Planner.graph`), which binds every
    package (`pkg.at(scope)` clones) and asks every Tilck stack for
    its `dep_list`, itself O(N) with a `default?` per package
    (`tilck_stack.rb:61-65`). With S stacks and N packages one graph
    costs O(S * N); `dependency_tokens` (`package.rb:929-934`) and
    `deps_build_env` (:1980-1989) each rebuild it once per build.
    Memoise per (registry generation, scope).
  * Fixpoint iteration `MAX_PASSES = 16` (`version_solver.rb:67`).
    Each pass can settle one more level of version-dependent
    dependency lists (`dep_list_for(ver)`); a chain of more than 16
    pin-dependent hops raises `UnstableError`. Two packages vary
    their list today (`host_gcc.rb:127-140`, `qemu.rb:149-159`). At
    1000 packages with per-version dependencies this is plausible;
    bound it by the closure depth instead.
  * Sysroot recomposition is a full rebuild of a symlink farm
    (`sysroot.rb:91-95`: `rm_rf` then recreate) for the stack touched
    after every build (`executor.rb:197-204`) and for EVERY stack
    after any removal (`executor.rb:76-79`). One stack of 45 packages
    is 4,892 links today (counted); 1000 packages is roughly 100k
    per stack, and a single `-u` then recreates 100k * (number of
    stacks) symlinks. Minutes, not milliseconds. Needs incremental
    composition (diff the owner map) and per-stack scoping of the
    removal path. On APFS symlinks are fine; case-insensitive APFS
    would silently merge two paths differing by case that the owner
    map (`sysroot.rb:137-142`) treats as distinct. No hardlinks are
    used anywhere, so filesystems without them are fine.
  * Version coexistence and pins: several versions of one package
    are siblings; a plan holds one version per name
    (`version_solver.rb:38,167-187`: two disagreeing pins are a
    `ConflictError`), and a request naming two versions is split into
    rounds (`planner.rb:1045-1058`). That is exactly the invariant a
    sysroot with one `usr/lib` needs. HOLDS.
  * Several host compiler stacks at once: a `:stack` package is read
    from every stack on disk (`package.rb:2028-2073`), the request's
    stack is the bound `host_gcc` (`planner.rb:148`), `-H` moves the
    default. HOLDS, proven by the six stacks present.

### 3.4 Scenario 4: a Linux distro instead of Tilck

  * `<machine>` for the target. Today `linux-x86_64` MEANS the build
    host: `world.rb:161-175` treats any machine that is not `noarch`
    and not `tilck-*` as a host, and `on_host` is a boolean on the
    package (`package.rb:418-445`). A Linux target for x86_64 must
    therefore be spelled with the product name, `mylinux-x86_64`,
    not `linux-x86_64`. That is a value, and the schema holds; but
    the choice is forced by a parse in `World`, not by the schema's
    definition of machine ("where it runs"). Document it: the machine
    value of a target is `<product>-<arch>`, never a bare OS name.
  * The target-side sysroot. A distro root is a composed sysroot at
    TARGET coordinates. `Coords#sysroot` is defined for any
    coordinates (`coords.rb:133`), but composition is host-only:
    `stack_coords` hardcodes `HOST_OS_ARCH` (`package_manager.rb:
    870-881`), `compose_stack_sysroot` (:988-1020) and
    `sysroot_fragments` (`package.rb:1427-1450`) are `:stack`-tier
    only, and the recipe token `$SYSROOT` is `""` for a target
    package (`package.rb:873`). The design doc already says "the
    sysroot IS a filesystem root" (`toolchain5.md:157-158`).
    Generalising composition to target coordinates is code, not
    schema. HOLDS WITH VALUE-LEVEL EXTENSION.
  * Cross compiler as a stack. Tilck's cross compiler is a
    `:portable` host package named `gcc-<arch>-musl` (`gcc.rb:37-41`)
    injected as a dependency by name (`planner.rb:42-51,709`,
    `package_manager.rb:247-249`, `main.rb:958,965`) and put on PATH
    by `with_cc` (`package_manager.rb:294-317`, which also builds a
    target `Coords` by hand at :297-298). A distro would want the
    cross toolchain to be the target's `<stack>` in fact, not just
    in name (`gcc-13.3.0` today names a prebuilt musl-cross-make
    blob). One owner is needed: `Architecture#cross_cc` naming the
    package, and `Coords.stack_name` used for targets too.
  * State (`/etc`, users) is a declared non-goal
    (`toolchain5.md:476-479`). A distro image builder would live
    beside the manager, as Tilck's CMake does.

What is Tilck-specific, by file: see section 5.

### 3.5 Scenario 5: "mylang" dependency and build manager

The pure core, with no `Package` and no globals except where noted:

  * `version.rb` (grammar is the limiter; see item N),
    `dep_resolver.rb` (graph as a hash, bounded walks),
    `version_solver.rb` (pins and defaults; no ranges),
    `build_env.rb` (neutral include/lib/pkg-config data, merged),
    `recipe.rb` (steps as data, canonical digest at :869-904, tokens
    at :718; `$PAR` is read from a global `BUILD_PAR`),
    `sysroot.rb` (pure filesystem), `install_selector.rb`,
    `plan.rb` (values; `Build` carries a `Scope`, :28),
    `coords.rb` (except `stack_ver`/`stack_name`, which know `gcc-`),
    `cache.rb` (reads `TC_CACHE`, `OS`), `build_inputs.rb` (reads
    `TC`, `MAIN_DIR`), `portability.rb` (x86_64 constants),
    `system_pkgs.rb` and `system_deps.rb` (host package managers,
    generic), `postcondition.rb`.

What a mylang manager needs that this does not have:

  * Range constraints (`>=1.2 <2`) and a solver that chooses. The
    solver's contract is "an explicit pin beats a default; two pins
    that disagree are an error" (`version_solver.rb:57-64`). For a
    language ecosystem with 1000 libraries this is the piece that
    would be replaced, not extended: the fixpoint loop over
    `dep_list_for(ver)` (:79-101) is a solver for one shape of
    problem. BREAKS for that use; the rest of the pipeline (plan,
    executor, records, sysroot as a view over selected versions) is
    exactly what such a manager wants.
  * The C++ ABI as a stack. `stack` = compiler x standard library x
    flags (`clang-18-libc++`, `gcc-14-libstdc++-lto`). The schema
    says a stack is free-form; the code says `gcc-<Version>` (item
    A). Fix item A and the mapping is one-to-one: machine
    `linux-x86_64`, env `any` (self-contained) or a distro, stack
    the ABI id.
  * A `Package` base class without `on_host`, `host_tier`,
    `with_cc`, `other/bsp`, `scripts/patches/<host_|target_>` naming
    (`package.rb:1595-1611`), `other/pkg_versions`
    (`package_manager.rb:27-30,1283-1312`). The placement policy
    (tier -> coordinates, `package.rb:563-599`) should be a data
    table handed in by the product, not a `case` in the base class.
  * Globals: `TC`, `TC_CACHE`, `TC_STAGING`, `MAIN_DIR`, `ARCH`,
    `BOARD`, `HOST_*`, `BUILD_PAR` (`early_logic.rb:360-401,490`).
    The tests swap `TC` by reassigning a constant
    (`coords.rb:102-118`). A `Host` value and an `Env` value passed
    in (the `Scope` is already the right shape, `scope.rb:26`) would
    remove the last global reads from the core.

Verdict: HOLDS as an extractable core; the product layer must be
separated first (section 5), and the solver is the one abstraction
that would not survive.

### 3.6 Scenario 6: the cache

See section 4. HOLDS for naming, resumability, upstream renames and
growth; BREAKS for integrity and concurrency.

## 4. The cache

Layout: `toolchain5/cache/` flat, plus `partial/` (resume) and `tmp/`
(extraction). 98 files today, every kind of source in one namespace.

  * Naming. Default `<name>-<ver>.tgz` for git clones packed by us
    (`source_ref.rb:78-80`, `cache.rb:438-498`), overridable per
    source (`tarname:`), with a separate `remote_tarname:` (:82-84)
    for the upstream's spelling. Two sources with the same bare
    version do not collide because the `SourceRef` name is the
    prefix (`gmp-6.2.1.tar.bz2` vs `mpfr-4.1.0.tar.bz2`). Two sources
    with the same NAME and different upstreams would collide, and
    nothing registers `SourceRef`s: they are constants, uniqueness is
    by convention. A registry of source names, checked at load like
    packages are (`package_manager.rb:223-232`), closes that.
  * Upstream renames. Absorbed by changing `remote_tarname` without
    touching the cache name (`host_gcc.rb:15-19`, `pcre2.rb:13-14`,
    `fribidi.rb:13-14`). HOLDS. GitHub tag archives are qualified by
    the package's own name (`libepoxy.rb:13-14`,
    `licheerv_nano_boot.rb:36-37`, `sophgo_tools.rb:31-32`), since
    the upstream name is `<tag>.tar.gz`. HOLDS.
  * Host-specific blobs. The cross compilers (`gcc.rb:114-130`) and
    CPython (`host_python.rb:26-40`) put the host OS and arch in the
    file name by their own convention; the bootstrap Ruby does too
    (`toolchain5.md:114-120`). `sophgo_host_tools-1.6.tar.gz`
    (x86_64 ELFs) does not, but the package is refused elsewhere
    (`sophgo_tools.rb:49-51`). No per-host or per-arch subdirectory
    is needed: the cache holds sources, and the few binaries name
    their host. HOLDS by convention; a `SourceRef` flag
    `host_specific: true` that appends `HOST_OS_ARCH` automatically
    would make it a rule.
  * Resumability. HTTP: `partial/<file>` plus `Range`
    (`cache.rb:165-195`, 206 at :120, 200-restart at :123-129, 416
    at :141-149), moved into place on success (:179-181). Git:
    retried, not resumable (`cache.rb:242-319`). Good.
  * Verification. None. No checksum, no size, no signature, no
    recorded commit for a tag clone beyond `.ref*` files written into
    the extracted tree (`cache.rb:299-311`). `download_file` treats
    any existing file as valid (`cache.rb:337-344`). Consequences: a
    tag that upstream moves yields a different tarball with the same
    name on the next fresh cache; a file truncated by anything
    outside the partial protocol (a crash during `mv`, a copy) is
    trusted forever; the `.build_inputs` digest covers the recipe and
    the patches, never the source (`build_inputs.rb:74-85`), so two
    trees can report `ok` from different sources. BREAKS for a
    ten-year archive. Fix: a `sha256:` on `SourceRef` (or a
    `cache/SHA256SUMS` index keyed by cache name) checked after every
    download and recorded in `.build_inputs` as `source sha256:...`.
  * Concurrency. `cache/tmp` is one directory, deleted by whoever
    arrives second (`cache.rb:372-379`, `:459-466`); `partial/<file>`
    is appended by any concurrent downloader (:167-177); no lock. Two
    pkgmgr processes on one tree (two shells, two hosts on NFS, `-a
    ALL` split across CI runners) corrupt each other. Fix:
    `cache/tmp.<pid>` and an `flock` on `partial/<file>`.
  * Growth. A flat directory of a few thousand tarballs is fine on
    every filesystem in use. The only listing of it is
    `Dir.children` in tests. HOLDS.
  * Sharing across arches. Source tarballs are arch-independent and
    the blobs name their host; the cache is safe to share and to
    survive `--clean` (`toolchain5.md:504-506`). HOLDS.

## 5. Generic core vs Tilck layer

| file / class                         | side    | what ties it |
|--------------------------------------|---------|--------------|
| version.rb, dep_resolver.rb          | core    | none |
| version_solver.rb                    | core    | none (pins only) |
| recipe.rb, build_env.rb, plan.rb     | core    | BUILD_PAR global |
| sysroot.rb, install_selector.rb      | core    | none |
| postcondition.rb                     | core    | Package::BuildCtx |
| coords.rb                            | core    | `gcc-` in :78,:87 |
| cache.rb, build_inputs.rb            | core    | TC/MAIN_DIR globals |
| system_pkgs.rb, system_deps.rb       | core    | none |
| portability.rb                       | core    | x86_64 constants |
| world.rb                             | mixed   | `tilck-` :165; HOST_ARCH |
| scope.rb                             | mixed   | Architecture, board |
| arch.rb                              | product | Tilck arches, boards |
| early_logic.rb                       | product | ARCH/BOARD env, bsp |
| package.rb                           | product | tiers, with_cc, bsp, |
|                                      |         | host_/target_ patches |
| package_manager.rb                   | product | host_gcc, host_python, |
|                                      |         | gcc-<arch>-musl, versions |
| planner.rb                           | product | cross cc injection :45, |
|                                      |         | Tilck stacks, host_gcc |
| tilck_stack.rb                       | product | (meta-package logic is |
|                                      |         | generic; the name is not) |
| layout.rb, main.rb                   | product | CMake contract, CLI |
| gcc.rb, host_gcc.rb, glibc.rb, ...   | recipes | per-package |

What a "mylang" extraction takes, in order:

  1. A `Product` owner holding: the target machine spelling
     (`"tilck-"` at eight sites), the cross-compiler package naming
     (`"gcc-#{arch}-musl"` at five sites), the version-file locations,
     the BSP root, the stack meta-package naming.
  2. A `StackId` value (item A) so that `<stack>` is a value class
     with a canonical spelling, not `"gcc-#{ver}"` at five sites.
  3. The tier table as data handed to `Package` (item C's sibling).
  4. `Host` (os, arch, distro, cc) as part of `Scope` and read from
     there (item D).
  5. Replace `TC`/`TC_CACHE`/`TC_STAGING`/`MAIN_DIR` reads in the core
     files with a passed-in `Tree` value; the test harness then stops
     reassigning constants (`coords.rb:102-118`).

After those five, `Package`, `Planner`, `Executor`, `World` and the
records are product-neutral; only the solver's contract would still
be Tilck-shaped (one version per name, pins only).

## 6. Latent breaks and recommendations

Ranked by how much harder each becomes with time. "Now" means before
more stacks, hosts or arches are built into the tree; the price of
each item later is a full rebuild of the affected coordinates,
because RPATHs and interpreters bake the path (`host_gcc.rb:406-411`).

  A. The stack value grammar. The schema promises `gcc-14.4.0-lto` and
     `gcc-13.3.0-musl` (`toolchain5.md:146-148`, `coords.rb:22-26`);
     the code requires `gcc-<Version>`: `coords.rb:86-89`
     (`stack_ver`), `package_manager.rb:906-919` (`host_stacks`,
     filter_map on it), `package.rb:2080-2084` (`stack_dirs_of`,
     prefix), `:2098-2099` (`SafeVer(cc_dir.sub("gcc-", ""))`, skip
     on nil), and `future_install` (`package.rb:1482-1496`) which
     reads the compiler off the coordinates. A variant stack is
     invisible to its own package and unlisted. Fix now: a `StackId`
     value (compiler family, version, variant) with `parse` and
     `to_s`, owned by coords.rb; `stack_ver` becomes
     `StackId.parse(s)&.compiler_ver`; the five hand-spelled
     `"gcc-#{...}"` target sites call `Coords.stack_name`. Later
     price: every tree that contains a variant stack.
  B. The distro env is not an ABI identity. `ID-VERSION_ID`
     (`early_logic.rb:239-249`) changes on every point release of a
     rolling distro and orphans the six stack compilers plus every
     `:distro` tool; `HOST_CC` carries the patch version
     (`early_logic.rb:355`) and orphans `gtest` on every compiler
     patch. Evidence on disk: `linux-x86_64/omarchy-4.0.2/any/pkgs/
     ruby/` (empty) beside `omarchy-4.0.3`. Also `VERSION_ID` is
     required or the tool exits (:245-248); vanilla Arch has none.
     Fix now (it is a value, so the derivation is one function):
     define env as `<id>-<glibc major.minor>` on Linux (the libc IS
     the ABI the tier describes), `macos-<major>` on Darwin,
     `freebsd-<major>`; define `HOST_CC` as `<family>-<major>`. Write
     the rule in `docs/package_manager.md` under host tiers. Later
     price: rebuild of every `:distro`/`:compiler` install once.
  C. `host_gcc`'s stack is a convention, not a path fact
     (`host_gcc.rb:104,166,173`). The design's own escape hatch
     (`toolchain5.md:161-172`, a `stack.conf` manifest) is the right
     shape and should exist BEFORE a second kind of stack does: write
     `<machine>/any/<stack>/stack.conf` when the stack's compiler is
     installed (`executor.rb:197-204` is the place), naming the
     compiler install's coordinates and version, the libc package and
     version, and the variant flags. Readers (`show_stacks`,
     `packages_in_stack`, `sysroot_fragments` in host_gcc.rb) then ask
     the manifest, and the stack compiler may live anywhere. Cheap
     now; later it is a migration of every stack directory.
  D. The host is not a coordinate the code reads. `Scope` carries
     `host_os` and `host_arch` (`scope.rb:26`) and nothing reads them
     (zero hits for `scope.host_os`/`scope.host_arch`). `Package#
     coords` reads `HOST_OS_ARCH`, `HOST_DISTRO`, `HOST_CC` globals
     (`package.rb:566-587`); `stack_coords` reads `HOST_OS_ARCH`
     (`package_manager.rb:881`); `World` scans one host
     (`world.rb:174`). The model and the exhaustive lane therefore
     cannot enumerate a second host, which is exactly where a
     cross-host bug would hide. Fix now: a `Host` value
     (os, arch, distro, cc, abi) inside `Scope`, read by `coords`;
     `Scope.env` builds it from the globals at the CLI boundary, as
     it does for ARCH/BOARD. Then `World.scan` can be asked for "all
     hosts" by `--clean`.
  E. `VersionSolver::MAX_NAMES = 1_000` (`version_solver.rb:60,
     140-142`). Replace with a bound derived from the registry size
     plus roots, like `dep_resolver.rb:108-110`. Trivial now; a
     surprise failure at 1001 packages later.
  F. Quadratic install-count paths: `World#of`/`find`
     (`world.rb:58,78-82`), `install_graph` (`planner.rb:721-742`),
     `show_status_all` states (`package_manager.rb:423`), graph
     rebuilt per `dep_closure` (`package_manager.rb:1132-1134`).
     Index the world by name and memoise `Planner.graph` per scope.
     Any time; cost grows with the tree.
  G. Product strings spread over the core: `"tilck-"` at
     `package.rb:604,2096,2101`, `layout.rb:57`, `main.rb:362`,
     `package_manager.rb:297`, `planner.rb:436,465`, `world.rb:165`;
     `"gcc-#{arch}-musl"` at `planner.rb:45,709`,
     `package_manager.rb:248`, `main.rb:958,965`. One owner each
     (`Coords.target_machine(arch)`, `Architecture#cross_cc_pkg`).
     Cheap now; the cost later is scenario 4's first day.
  H. Cache integrity and concurrency (section 4): a `sha256` per
     source recorded in `.build_inputs`, `cache/tmp.<pid>`, an
     `flock` on `partial/<file>` and on `staging/<pkg>/<ver>`. The
     checksum is the one that gets more expensive later: every
     existing cache entry has to be hashed and blessed once.
  I. Records: `.install_origin` and `.built_against` have no format
     line (`package.rb:57-78,96-110`), and no record says at which
     coordinates or on which host it was written. A schema change
     can re-judge an install only from its path. Fix now: one
     `.install` file with `format`, the coordinates as written, the
     host identity (item D's value) and the stack manifest reference;
     keep `.build_inputs` as the comparable half. Absent-tolerant
     readers already exist (`build_inputs.rb:90-97`), so old trees
     keep reading.
  J. Relocation. Absolute symlinks (`sysroot.rb:144`) and absolute
     RPATH/interp (`host_gcc.rb:406-411`) mean a tree works at one
     path. State the rule in `docs/package_manager.md`: a shared tree
     is shared at one absolute path (`TCROOT_PARENT`,
     `early_logic.rb:168-174`); a copied tree keeps `cache/`,
     `noarch/` and `tilck-*/`, and the host world must be rebuilt.
     Relative symlinks in the farm (`../../pkgs/...`) would remove
     half of it at no cost; RPATH is the other half and is a design
     choice.
  K. Board-less target arches get env `any` (`arch.rb:88-96`,
     `main.rb:445-449` creates `tilck-aarch64/any/gcc-13.3.0/pkgs`
     on every run). Either give every arch a board (aarch64:
     `qemu-virt`) and assert `boards` non-empty in `Archs`, or stop
     `create_toolchain_dirs` from creating directories for arches
     with no board. Small now; a second meaning of `any` later.
  L. Host ABI constants for the audit and the loader:
     `portability.rb:117,169-172`, `host_gcc.rb:272,430`,
     `glibc.rb:88`, `package_manager.rb:934`, and the `EM_X86_64`
     comment already asks for it (:115-116). A `HostABI` table keyed
     by `HOST_OS_ARCH`. Needed for scenario 1; nothing to migrate.
  M. The tier `case` has no `else` (`package.rb:563-599`): an unknown
     `host_tier` yields `nil` coordinates and fails far away. Make
     the table data and raise on an unknown key.
  N. The `Version` grammar (`version.rb:108-120`) rejects `3.10.0-3`,
     `1.0_p1`, `2.0-beta` with a non-listed word, and any stack
     variant suffix. For Tilck it is fine; for scenarios 4 and 5 it
     is the first thing to hit. Add an opaque, string-ordered
     fallback type rather than raising, and forbid it only where a
     `series` is needed.
  O. Two stacks in one package cannot be expressed (`scope.stack`
     single, `$STACK_SYSROOT` single at `package.rb:879`). This is
     correct: one artifact has one ABI. No action; note it in the
     docs as an invariant, so nobody adds a second stack coordinate
     for a Canadian cross (placement already covers it,
     `toolchain5.md:150-151`).

## 7. What was not verified

  * No host other than this one (linux-x86_64, omarchy 4.0.3, gcc
    16.2.1) was run. Darwin, FreeBSD, aarch64 and vanilla Arch
    behaviour is read from the code, not observed; the `VERSION_ID`
    claim for Arch rests on the known shape of Arch's
    `/etc/os-release`, not on a test.
  * No scale test was run. The quadratic paths are identified by
    reading; the "seconds to tens of seconds" estimate at 3,500
    installs extrapolates from 0.24 s at 350 and is not measured.
  * The exhaustive lane, the model and the mutation suite were not
    executed; their scope (one host, worlds of up to three installs,
    `tests/exhaustive/domain.rb`) is taken from the code and docs.
  * `docs/plans/toolchain5.md` and `pkgmgr-functional-core.md` were
    skimmed for the rule, the escape hatch and the non-goals, not
    read end to end.
  * The recipes of the ~60 concrete packages were sampled (gcc,
    host_gcc, glibc, qemu, gnuefi, uboot, licheerv_nano_boot,
    ncurses, host_python, sophgo_tools, tilck_stack), not audited
    one by one for further host or arch assumptions.
  * Whether a `gcc-14.4.0-lto` directory is in fact skipped was
    concluded from `SafeVer` on `"14.4.0-lto"` failing the grammar at
    `version.rb:108-120`; it was not created on disk to observe the
    listing.
  * The CMake side was read only where it consumes `--print-layout`
    (`CMakeLists.txt:382,985-1053`) and the bash bootstrap only where
    it composes Ruby's path (`scripts/bash_build_toolchain:402-403`);
    both build one path by hand, as they say they must.
