# Package manager: a functional planning core

Status: PLAN (2026-09-13). Nothing below is in the tree. It follows
the correctness work in `pkgmgr-correctness-proof.md` (lint, model,
laws, exhaustive lane, mutation) and the recipe conversion in
`toolchain5.md` (every recipe is step data). Sizes are estimates.

## 0. What this is for

Two things, and the second is the one that pays.

**Bugs that cannot be written.** Every logic bug the package manager
has had was a question about one installation answered from ambient
state: the global `ARCH`, the global `BOARD`, the stack that happened
to be current, a version table stashed by an earlier call. The lint
and the mutator hunt those shapes after the fact. This makes them
unwritable: the scope becomes a parameter, the world becomes a value,
and there is no ambient state left to read.

**A proof that is cheap enough to widen.** The exhaustive lane asks
the implementation and the model the same question for every world
of at most two installations. Every case builds a fake toolchain on
disk, runs `Main.main`, and scans the tree back:

    test_a_sample_of_every_shape_agrees_with_the_model   4024 ms
    (1,000 cases: ~4 ms each; the full 66,426 take minutes)

With a pure planner the same case is two function calls over two
small Sets, tens of microseconds. That is what makes a bound of three
installations affordable, and three is the bound that matters the
day a bug needs three of something.

What this does NOT change: the model stays the spec, written to be
read, and stays an independent second implementation; the executor
(build, atomic move, uninstall, sysroot composition) stays imperative;
recipes are not rewritten.

## 1. The layer, drawn

Today one object does everything:

    Main.main(argv)
      pkgmgr.with_target_arch(a) {            # scope: an ivar
        pkgmgr.with_host_stack(s) {           # scope: an ivar
          pkgmgr.force_remove(...)            # reads disk, writes disk
          plan = pkgmgr.resolve_install_plan(requested)
                                              # reads disk, STASHES @resolved_versions
          pkgmgr.install(name, ver)           # reads @resolved_versions, builds,
        }                                     # writes records, recomposes sysroot
      }

After:

    Main.main(argv)
      req   = Request.parse(argv)             # pure
      scope = Scope.of(env, req)              # pure
      world = World.scan(registry)            # the ONE read of the tree
      plan  = Planner.step(registry, world, req, scope)   # pure
      print plan
      Executor.run(plan) unless req.dry       # the ONE writer

    tests/exhaustive:
      Planner.step(reg, world, req, scope).world == Model.step(...).world
                                              # values, no disk

Three values that do not exist today -- `Scope`, `World`, `Plan` --
and one module that owns nothing but decisions -- `Planner`. The
model already has all three under other names (`Scope`, a `Set` of
`Key`, the `entries` a `plan` returns); the product gets its own,
because the model must stay a second implementation.

## 2. The values

### 2.1 Scope

What the invocation resolves to. Same fields as `Model::Scope`
(`tests/model/model.rb:187`), and the model will reuse this class the
way it reuses `Coords` and `Ver`: a value object with its own tests,
deciding nothing.

```ruby
# scripts/pkgmgr/scope.rb
Scope = Data.define(:arch, :board, :stack, :env_arch, :env_board,
                    :host_os, :host_arch) do

  # The one rule about boards: the scoped board for the scoped arch,
  # the shell's BOARD for the shell's ARCH, an arch's default otherwise.
  def board_of(a)
    return board     if a == arch
    return env_board if a == env_arch && env_board
    return a.default_board
  end

  def with(arch: self.arch, stack: self.stack) = ...   # as the model's
end
```

Built once, at the CLI boundary, from the environment and the parsed
request. `-a` is a scope for the modes that build and a filter for
`-u`; `-H` names the stack; both already have exactly this reading in
`Model.scope` (`model.rb:211`) and `Main#requested_arch` /
`select_host_stack` (`main.rb:1028`, `main.rb:1051`).

### 2.2 A package, bound to a scope

The problem, as it stands:

```ruby
# package.rb:432 -- the answer depends on which block is open up the stack
def coords(ver = nil)
  ... default_arch ... target_board(a) ... stack_gcc_ver(ver)
end
def default_arch = pkgmgr.target_arch                    # package.rb:1399
def target_board(arch) = pkgmgr.board_for(arch)          # package.rb:489
def stack_gcc_ver(ver = nil) = pkgmgr.current_host_stack # package.rb:517
```

```ruby
pkgmgr.with_target_coords(rv, "licheerv-nano") { pkg.coords }   # right
pkg.coords            # today: whatever scope is open, else ARCH / BOARD
```

Proposed: a package in the registry is a declaration and answers no
scoped question. Asking one binds it first:

```ruby
class Package
  # A copy of this package that answers every scoped question from
  # `scope` and nothing else. Cheap: a shallow dup with one field set.
  def at(scope)
    b = dup
    b.instance_variable_set(:@scope, scope)
    return b
  end

  def scope
    raise Unbound, "#{name}: asked a scoped question unbound" if @scope.nil?
    return @scope
  end

  def default_arch = scope.arch                 # was pkgmgr.target_arch
  def target_board(arch) = scope.board_of(arch) # was pkgmgr.board_for
  def stack_gcc_ver(ver = nil) = scope.stack    # was current_host_stack
  # coords, default_cc, supported?, install_dir, build_tokens, ...:
  # unchanged bodies, now reading the three above.
end

pkg.at(scope).coords(ver)       # visible at the call site, always
pkg.coords(ver)                 # raises Unbound
```

Why `dup` and not a parameter on every method: 55 package files
call `default_arch` inside recipes (`"AR=#{default_arch.gcc_tc}-linux-ar"`
in zlib; gnuefi five times), and `build_steps`, `build_tokens`,
`expected_files`, `dep_list_for`, `installable_versions` all read
scope somewhere. Threading a parameter through the recipe DSL would
touch every recipe for no gain in visibility: `self` on a bound
package IS the scope, one field, immutable, set by the caller that
holds it. The lint's job ("who reads `ARCH`?") becomes a type
question ("is this package bound?"), and the answer is checked at
runtime by `Unbound` and at test time by a case per scoped method.

The overrides that read scope today, and what they become:

| today | after |
|---|---|
| `HostGccPackage#default_ver = pkgmgr.current_host_stack` | `= scope.stack` |
| `HostGccPackage#stack_gcc_ver(ver) = ver \|\| current_host_stack` | `= ver \|\| scope.stack` |
| `TilckStackPackage#dep_list` opens `with_target_coords(@arch, @board)` | `dep_list_for(ver)` on `at(scope.with(arch: @arch))`, the registry asking `pkg.at(scope).dep_list_for(ver)` everywhere (model: `Registry#deps_of(name, scope)`) |
| `Package#with_install_context(inst) { }` | `at(Scope.at_install(scope, inst))`: the install's arch, board and stack put into a scope, as `Model.scope_at` (`model.rb:788`) |
| `PackageManager#with_target_arch/with_target_coords/with_host_stack` | deleted |
| `PackageManager#target_arch/board_for/current_host_stack/host_stack=` | deleted |

### 2.3 World

The problem: what is installed is read per package, cached per
package against a generation counter, and refreshed by whoever
remembers to (`refresh`, `installs_changed!`: 24 sites).

```ruby
# package.rb:1359
def get_install_list
  gen = pkgmgr.tree_generation
  if @installs.nil? || @installs_gen != gen || !@installs_tc.equal?(TC)
    @installs = read_install_list.freeze ...
```

Proposed: one scan, one value, passed in.

```ruby
# scripts/pkgmgr/world.rb
World = Data.define(:installs) do            # frozen Array of InstallInfo

  # The tree, read once. Every coordinates every package could be at
  # (what read_install_list walks today), records and origins read
  # beside each install. Broken installs are kept and flagged, as now.
  def self.scan(registry) = new(installs: registry.packages.flat_map(&:read_install_list).freeze)

  def of(name)                  = installs.select { |i| i.pkgname == name }
  def at(name, ver, coords)     = of(name).find { |i| i.ver == ver && i.coords == coords && !i.broken }
  def installed?(pkg, ver)      = pkg.install_coords(ver).all? { |c| at(pkg.name, ver, c) }
end
```

`InstallInfo` gains `record` (`:ok | :changed | :old_format | :missing`),
computed at scan time by the package at the install's own scope --
which is what `build_inputs_state_of` does today, one install at a
time, from `-l` and `--check-for-updates`. The scan is the only place
that judges records; the planner reads the field.

`Package#find_install(ver)` / `installed?(ver)` / `install_prefix(ver)`
become `world.at(name, ver, at(scope).coords(ver))` and friends: a
world and a scope, both visible.

### 2.4 Plan

The problem: the plan is half a return value and half a stash.

```ruby
# package_manager.rb:1590
def resolve_install_plan(requested_pairs)
  versions = resolved_versions_for(requested_pairs)
  @resolved_versions = versions        # read later, from any depth, by builds
  ...
  ordered_names.map { |name| [name, ...] }
end
```

Proposed: one value carries everything the execution needs.

```ruby
# scripts/pkgmgr/plan.rb
Build  = Data.define(:name, :ver, :coords, :origin, :mark, :against)
                                # against: {dep => ver} -- what InstallDeps records
Remove = Data.define(:install)
Mark   = Data.define(:install, :mark)
Recompose = Data.define(:stack)

Plan = Data.define(:actions, :scope, :bound, :rc, :message) do
  def dry?  = actions.empty?
  # The world after, as the executor is obliged to leave it. What the
  # exhaustive lane compares to the model, and what the executor's
  # own test compares to a rescan.
  def apply(world) = ...            # pure: fold the actions over the Set
end

Refusal = Data.define(:rc, :message)   # "Version conflict: ...", "not supported here"
```

A dry run is `puts plan` and no `Executor.run`. The law that `-d`
touches nothing holds by construction; the fingerprint test in the
suite stays, as a check on the executor rather than on every mode.

## 3. The planner

`scripts/pkgmgr/planner.rb`, `module_function`, pure. Every function
takes `(registry, world, req, scope)` and returns a `Plan` or a
`Refusal`. It names no package, reads no global, calls nothing that
touches the disk, and holds no state. It corresponds one to one to
the model's transitions (`model.rb:444` onward), but is written from
the implementation's current logic, NOT copied from the model: the
lane compares two implementations, and a copy proves nothing.

| mode | today | planner function | executor actions |
|---|---|---|---|
| `-s X[:V] ... [-f] [-a A] [-H S]` | `main.rb:1432-1660`, `resolve_install_plan`, `force_remove`, `mark_requested_manual`, `install` | `plan_install` | `Remove*` (from `-f`), `Mark*`, `Build*`, `Recompose*` |
| `-s X -a ALL` | the loop at `main.rb:1440` | `plan_install_every_arch` (threads the world) | same |
| (no mode) | `main.rb` default path | `plan_default` | same |
| `-u X[:V] [-a] [-c]`, `-U`, `--clean` | `uninstall_selector`, `uninstall_where`, `uninstall` | `select_uninstall` → `plan_uninstall` | `Remove*`, `Recompose*` |
| `--mark-manual/--mark-auto` | `mark`, `mark_requested_manual` | `plan_mark` (the same selection as `-u`) | `Mark*` |
| `--autoremove` | `autoremove`, `install_graph`, `held_by` | `plan_autoremove` | `Remove*` |
| `--upgrade` | `get_upgradable_packages` + the plan | `plan_upgrade` | `Build*` |
| `--rebuild` | `main.rb:1319-1410`, `get_stale_installs`, `deps_of_install`, `replace` | `plan_rebuild` | `Build*` at the install's own coords |
| `--check-for-updates` | `main.rb:1156` | `check_updates` → text + rc | none |
| `--list-installable`, `--print-layout`, `-l` | scattered | `installable`, `layout`, `listing` → values | none |

Two helpers the planner owns and nothing else does:

```ruby
# Versions bound for a request, pins and defaults resolved together
# (VersionSolver stays as is; it is already pure).
def bind(registry, roots, scope) -> {name => ver}      # or Refusal

# What `-u` / `--mark-*` name. Total: a subset of world.installs.
def select(registry, world, req, scope) -> [InstallInfo]
```

`InstallSelector` and `CoordsFilter` (`install_selector.rb`) stay;
`uninstall_where` moves into the planner unchanged in logic, with
`target_arch` and `board_for(a)` read from `scope`.

The `-f` rule, as an example of what "structural" means here:

```ruby
# today: two halves that have to agree, and twice did not
pkgmgr.force_remove(name, ver)          # asks pkg.coords under the open scope
plan = pkgmgr.resolve_install_plan(..)  # asks pkg.installed? under the open scope

# after: one scope, one world, one function
def plan_install(registry, world, req, scope)
  bound  = bind(registry, req.targets, scope)
  scope  = scope.with(stack: bound["host_gcc"] || scope.stack)
  gone   = req.force ? req.targets.flat_map { |n, v| installs_of(world, n, v, scope) } : []
  world2 = world.without(gone)
  order  = DepResolver.resolve(...)      # cut at what world2 has, at bound versions
  ...
  Plan.new(actions: gone.map { Remove.new(_1) } + builds, scope:, bound:, ...)
end
```

The removal and the plan cannot disagree about which installation
this is, because both read `scope` and `world` from the same two
arguments. The check at `main.rb:1573` ("`-f` removed nothing that
the install would recreate") becomes a one-line assertion in a unit
test rather than a runtime guard.

## 4. The executor

`scripts/pkgmgr/executor.rb`. Everything that is imperative today
stays imperative here, moved rather than rewritten:

```ruby
module Executor
  def run(registry, plan)
    for a in plan.actions do
      case a
      when Build     then build(registry[a.name].at(plan.scope.for(a)), a)
      when Remove    then remove(a.install)
      when Mark      then InstallOrigin.write(a.install.path, ..., a.mark)
      when Recompose then compose_stack_sysroot(a.stack)
      end
    end
  end
end
```

`build` is today's `PackageManager#install` from `pkg.install_impl(ver)`
down (`package_manager.rb:886-948`): the atomic staging, the origin
and deps records, `write_build_inputs` for every arch the install
writes, the sysroot recomposition, the portability audit, the
postconditions. It takes `a.against` for `InstallDeps` instead of
reading `@resolved_versions`, and `a.coords`/`plan.scope` for where
to write instead of the open scope.

Its contract, checked by its own test lane (§6.2): after `run`,
`World.scan(registry)` equals `plan.apply(world)` for the installs
the plan names, record `:ok`.

## 5. The steps, in order

Each step is one commit, suite green, real install exercised where
the step touches the build path. Each step also RATCHETS the lint:
`test_lint_ambient`'s allowlist and the count of `with_*` sites can
only shrink, and the number at the end of each step is written in
its commit message. The arc is not done until §5.6 deletes the ivars;
a tree with both styles is worse than either, because the fallback IS
the ambient read.

### 5.1 Scope and the bound package (~450 lines)

- `scope.rb`: the value, `Scope.of(env, req)`, `Scope.at_install(scope, inst)`,
  `board_of`, `with`. Tests: the three board rules, `with` keeps the
  environment fields.
- `Package#at`, `#scope`, `Unbound`. `default_arch`, `target_board`,
  `stack_gcc_ver`, `default_cc` read `scope`. `HostGccPackage`,
  `TilckStackPackage`, `GccPackage` overrides likewise.
- `PackageManager#with_target_arch/with_target_coords/with_host_stack`
  are reimplemented, for this step only, as `pkgmgr.scope = ...` around
  the block, and `target_arch/board_for/current_host_stack` read
  `pkgmgr.scope` -- so that everything not yet converted keeps working
  through ONE ivar instead of three, and the lint counts its readers.
- `Package#coords(ver)` and every method under it: bodies unchanged.
  Every caller that has a scope in hand calls `pkg.at(scope).coords`;
  the ones that do not yet call `pkg.at(pkgmgr.scope).coords` and are
  listed in the allowlist with the step that removes them.
- Test: every scoped method raises `Unbound` on a registry package.
  The model starts reusing `Scope` (delete its own `Data.define`).

### 5.2 World (~350 lines)

- `world.rb`: `World`, `World.scan`, lookups. `InstallInfo#record`.
- `Package#find_install/installed?/install_prefix` take a world:
  `installed?(world, ver)`. `install_prefix` is read by recipes through
  `dep_install_dir` and the `$dep` tokens -- those get the world from
  the bound package (`at(scope, world:)`), one more field.
- `PackageManager`: `@known_installed`, `@installable`,
  `@known_pkgs_paths`, `@tree_generation`, `installs_changed!`,
  `refresh` go; `orphan_installs` comes from the scan. `Bridge.world`
  reads `World.scan` and turns it into keys (no `refresh` dance).
- Test: `World.scan` twice is equal; a world built in memory answers
  the same lookups as one scanned from a fake tree built by `fake_install`.

### 5.3 The install planner (~600 lines, ~300 of them deleted from main.rb)

- `plan.rb`: the action types, `Plan`, `Plan#apply`, `Refusal`.
- `planner.rb`: `bind`, `plan_install`, `plan_install_every_arch`,
  `plan_default`. `resolved_versions_for`, `resolve_install_plan`,
  `mark_requested_manual`, `force_remove`'s coordinate rule and
  `built_against` move here as pure functions; `@resolved_versions`
  and `with_resolved_versions` are deleted; `Build#against` carries
  what the builds read.
- `executor.rb`: `run`, `build` (today's `install` from `install_impl`
  down), `remove`, `mark`, `recompose`.
- `main.rb`: the `-s` path (`1432-1660`) becomes parse, scope, scan,
  plan, print, run. The dependency-tree printout and
  `SystemDeps.check_plan` read the `Plan`.
- Real builds: `-s host_gperf`, `-s host_gcc -d`, `-s zlib -a riscv64`
  on this machine; `--check-for-updates` 0 after each.

### 5.4 Removal, marks, autoremove (~350 lines)

- `select` (today's `uninstall_selector` + `uninstall_where`),
  `plan_uninstall`, `plan_mark`, `plan_autoremove`, `plan_clean`.
  `install_graph`/`held_by`/`needs_of_install` become pure over a
  `World`. `PackageManager#uninstall` from `to_remove` down
  (`package_manager.rb:1910-1990`) moves to `Executor#remove`.
- Dry-run: `-u ... -d` prints the plan and runs nothing.

### 5.5 Upgrade, rebuild, the observations (~350 lines)

- `plan_upgrade`, `plan_rebuild` (today `main.rb:1319-1410`,
  `get_stale_installs`, `deps_of_install`, `replace` -- the set-aside
  of the old tree stays in the executor), `check_updates`,
  `installable`, `layout` (`layout.rb` reads the scope it is given;
  CMake's `--print-layout` builds it from the environment).
- `-l` keeps its rendering; it reads a `World` and a `Scope`.

### 5.6 Deletion (~250 lines removed, ~64 test sites rewritten)

- `PackageManager`: `@scope` and every `with_*`, `target_arch`,
  `board_for`, `current_host_stack`, `host_stack=`,
  `with_install_context` go. Nothing reads `ARCH`/`BOARD` outside
  `early_logic.rb` and `Scope.of`.
- Tests: the 64 `with_*` sites in 18 files become `at(scope)` calls
  or `Scope.new(...)` values; `test_helper`'s `run_cli` builds the
  scope the way `Main` does.
- Lint: R1 keeps only `early_logic.rb` and `Scope.of`; R3 has nothing
  left to check and is deleted with a note; the mutator's O9 and O10
  are deleted with a note -- there is no `with_target_coords` to
  narrow and no `prev` to drop. `pmmutate count` before and after go
  in the commit message.
- Docs: `package_manager.md` "Correctness guarantees" and the
  "reads its inputs through their owners" section of `CLAUDE.md`
  say what the rule is now: a scoped question is asked of a bound
  package, and the registry package raises.

### 5.7 The lanes (~500 lines)

- `tests/exhaustive/`: a case is `(shape, world, ctx, argv)` as now;
  the world is built in memory (`World.new` from the candidates, no
  `fake_install`), the argv is parsed by `Main`'s parser into a
  `Request`, and the comparison is
  `Planner.step(reg, world, req, scope).apply(world) == Model.step(...).world`
  plus rc and, where the model says something, the message class.
  Self-test as now: the planner is deterministic, a planted
  disagreement is seen, and -- new -- `Plan#apply` on an empty plan is
  the identity.
- Bound: `worlds(cands, max: 3)`. Budget: the full lane under `-t`
  in seconds; if it is under ten, it replaces the sample.
- `tests/test_executor.rb`: for each action kind, one disk case:
  build a world with `fake_install`, run one action, and assert
  `World.scan == plan.apply(world)` for the installs it names. This
  is the ONLY lane that touches the disk for the sake of logic, and
  it grows per action kind, not per case.
- The laws (`tests/laws.rb`) stay around every `run_cli`: L1 now
  compares the planner's `apply` and the rescan against the model
  both -- two disagreements, two different bugs.

### 5.8 Measure

Before 5.1 and after 5.7, on this machine: the full exhaustive lane's
wall time and case count, `pmmutate count`, the suite's wall time.
The numbers go in `package_manager.md`.

## 6. What becomes structural, what stays semantic

| historical bug | today's shape | after |
|---|---|---|
| `board_supported?` read global `BOARD` under a scoped install | `pkgmgr.board_for(arch)` inside `with_install_context` | structural: no global; the bound package has one board |
| one arch's recipe digest recorded for both | `default_arch.gcc_tc` under the wrong scope | structural: the scan judges each install at `Scope.at_install` |
| stack B's sysroot composed from stack A's installs | `current_host_stack` inside composition | structural: `Recompose(stack)` names its stack |
| `-f` removed six GCCs to rebuild one | the selector matched on part of a key | semantic: `select` + the model |
| `-f` removed nothing and the plan found the install (twice) | removal and plan under different scopes | structural: one scope, one world, one function |
| gmp built at its default, not the version gcc pinned | `@resolved_versions` set in one block, read in another | structural: `Build#against` |
| `-u X:V` exited 0, removed nothing, said nothing | selection with a `nil` compiler read as "must be nil" | semantic |
| `-s ALL` for the second board installed nothing | `find_install` matched (compiler, arch), not `Coords` | semantic (`Coords#==`), already fixed |

Semantic rows stay with the model and the lane. What the conversion
buys them is a lane that runs the whole domain in seconds, so the
bound can go up.

## 7. Risks, and what answers them

- **Two planners converging.** If `planner.rb` is written by reading
  `model.rb`, the lane compares a function with itself. Rule: the
  planner is written from `package_manager.rb`/`main.rb` as they
  stand, function by function, and the first lane run after each is
  expected to show the SPEC disagreements the model already marks
  (`model.rb` has three `SPEC` notes where the implementation is
  wrong). Those are fixed as separate commits, with the model's
  expected answer, not by editing the model.
- **A half-converted tree.** §5.1 collapses three ivars into one on
  purpose, so that the count of its readers is a single number the
  lint prints and each step's commit message records. §5.6 does not
  land until it is zero.
- **`dup` and package state.** A bound package shares no mutable
  state with the registry package: the only ivars that mutate today
  are the install-list caches, which §5.2 removes. Test: `at(scope)`
  twice gives equal answers and leaves the original unbound.
- **Recipes reading the world.** `dep_install_dir`, `$PREFIX`,
  `install_prefix` are read while a recipe RUNS, from the bound
  package; the world at that moment is the executor's current one,
  rescanned after each build. That is the same freshness `refresh`
  gives today, made explicit.
- **The `-a ALL` loop and `install_every_arch`.** The model threads
  the world through the arches; the planner does the same, returning
  one plan per arch or one concatenated plan with per-action scopes.
  Decide by what the executor needs: one plan, each `Build` carrying
  its coords, is simpler to run and to print.
- **CMake and `--print-layout`.** Reads `ARCH`/`BOARD` from the
  environment to build its scope; that is the CLI boundary, allowed.

## 8. Not in this arc

- Recipes taking a scope parameter, or replacing `default_arch.gcc_tc`
  with a `$GCC_TC` token so that a digest no longer depends on the
  arch at all. Worth doing (it would make `Scope.at_install` unneeded
  for judging records) and separable.
- The system tests (`--system-tests`), which drive real builds and
  keep their shape.
- `pmrecord`, which renders recipes and needs a bound package: one
  `at(scope)` in the tool.
