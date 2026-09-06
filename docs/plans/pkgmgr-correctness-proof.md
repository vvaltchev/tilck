# Package manager: zero logic bugs, with proof

Status: IMPLEMENTED (2026-09-03). Steps 0-7 are in the tree; the
numbers below were the plan's estimates, and the sections that
follow record what was found on the way.

  step 0  three ambient readers fixed, Layout.board_of deleted
  step 1  lint: tests/lint/ambient.rb, test_lint_ambient.rb
  step 2  model: tests/model/model.rb, test_model.rb (19 cases)
  step 3  InstallSelector, uninstall_selector / uninstall_where,
          gnuefi on coordinates; R2 pin empty
  step 4  laws: tests/laws.rb, run_cli in the helper, ~1,030
          command lines judged per run
  step 5  exhaustive: tests/exhaustive/, 66,426 cases (the
          multi-package shapes enumerate dependency structure, not
          coordinates -- see NARROW in domain.rb); found two
          implementation bugs on its first sample (the default
          install ignoring -d; the board checked after -f)
  step 6  pmmutate: 456 sites at first, 116 survivors, each answered
          by a test (test_survivors.rb), a simpler line, or an
          annotated reason; 318 sites now
  step 7  the six toolchain workflows run -t --exhaustive;
          docs/package_manager.md "Correctness guarantees"; CLAUDE.md

## 0. The claim to be established

For the package manager's *logic* -- placement, filtering, scoping,
dependency resolution, version resolution, staleness, listing -- the
target is not "well tested". It is:

> For every registry shape in the catalogue, every world with at
> most two installations, every invocation context, and every
> command line in the grammar: the implementation's effect on the
> tree and its answer to every query are exactly what the model
> says.

checked mechanically on every commit, plus a certificate that the
tests would notice if any line of the logic core were wrong
(mutation score 100% on that core).

Out of scope, deliberately: whether an external package builds; the
contents of recipes; the network. Those change under us and are not
logic.

## 1. Why the bugs recur: one bug, many spellings

Every logic bug in this tree since toolchain5 has the same shape:
**a question about one specific installation, answered from ambient
state instead of from that installation's own coordinates.**

| command | did | should | defect |
|---|---|---|---|
| `-H 14.4.0 -u host_qemu:6.2.0` | nothing, silently | remove the 14.4.0-stack tree | `default_cc == "syscc"` as a proxy for "host package" |
| `-f -s host_qemu:6.2.0` | removed 6.2.0 AND 11.1.0 | remove 6.2.0 | `nil` passed for compiler, which means "all" |
| `-u zlib` on riscv64 | removed both boards | the board you're on | `e.arch == arch`: an arch is two thirds of a coordinate |
| `--check-for-updates` | 22 fresh packages `:changed` | `:ok` | recipe rendered at the current stack, not the install's |
| `-a riscv64 -f -s uboot` | `requires board qemu-virt`, after deleting it | rebuild | `board_supported?` read the global `BOARD` |
| `--check-for-updates` (latent) | board never scoped in `with_install_context` | judge at the install's board | third coordinate never scoped |
| six CI runs, 2026-09-03 | `host_python is not installed` | pass anywhere | test read the machine's tree |

Two more instances exist in the tree today, found by grep in one
minute after the last fix:

```ruby
# scripts/pkgmgr/layout.rb:48 -- the board rule, copy #2 (CMake's view)
def board_of(arch)
  return BOARD if arch == ARCH && BOARD
  return arch.default_board
end

# scripts/pkgmgr/package_manager.rb:1275 -- uninstall's filter:
# the OLD identity key (a tuple) with Coords bolted on as a fifth clause
to_remove = install_list.select { |e|
  (all_pkgs   || e.pkgname == name     ) &&
  (all_ver    || e.ver == ver          ) &&
  (all_arch   || e.arch == arch        ) &&
  (all_cc     || e.compiler == compiler) &&
  (board.nil? || e.coords.nil? || e.coords.env == board) &&
  (coords.nil?|| coords.include?(e.coords)) && ...
```

Structural cause: two identity mechanisms coexist. `Coords` was
introduced as THE identity of an installation, and the migration was
never finished or enforced. Each new test finds another site still
using the old key.

Remaining ambient readers outside the owners (the complete list, from
`grep -n '\bARCH\b\|\bBOARD\b' scripts/pkgmgr/*.rb`):

| site | reads | disposition |
|---|---|---|
| `gcc.rb:95,99` `GccPackage#default?` | `ARCH` | read `pkgmgr.target_arch` |
| `package_manager.rb:407` `show_status_all` | `ARCH.gcc_ver` | read `target_arch.gcc_ver` |
| `package_manager.rb:1182-3` uninstall ALL/orphan | `ARCH` | removed by the selector refactor (step 3) |
| `layout.rb:48` `Layout.board_of` | `ARCH`, `BOARD` | DELETE; call `pkgmgr.board_for` |
| `layout.rb:61,62,73` `Layout.vars` | `ARCH`, `BOARD` | allowlisted: it *reports* the invocation to CMake |
| `package_manager.rb:50,81` `target_arch`, `board_for` | both | the owners |
| `early_logic.rb`, `main.rb` | both | definitions and the CLI boundary; allowlisted per method |

## 2. What the four mechanisms, as first stated, were missing

The first statement (session of 2026-09-03) listed: (1) a lint against
ambient reads, (2) a universal postcondition, (3) a reference model
with bounded-exhaustive differential testing, (4) mutation testing.
Re-reading it against the code:

- **M1. Two oracles.** The postcondition's `matching(request)` and
  the model's selector are the same function. Written twice, they
  can agree wrongly. There is ONE definition: the model's. So the
  model comes before the postcondition, not after.
- **M2. Observations, not only transitions.** Half the bugs were
  wrong *answers* with no change to the tree: `:changed`, "already
  installed", the `-l` listing, `--check-for-updates` exit 2,
  `--print-layout`. The model is a state machine with
  `step(world, argv) -> world'` AND `observe(world, query) -> value`.
- **M3. The registry is an input.** Install semantics depend on
  deps, pins, tiers, `arch_list`, `board_list`, `default:`,
  `host_world_root?`, `install_archs`. The enumeration is over
  (registry shape, world, context, argv), not (world, argv).
- **M4. The context is an input.** `ARCH`, `BOARD`, `-a`, `-H`,
  `HOST_VER_GCC`. The lint is what makes this list complete: once
  it passes, the implementation has no other inputs, and the model
  can claim to enumerate them all. The lint is therefore a
  *prerequisite* of the theorem, not a parallel measure.
- **M5. Detection is not removal.** The tuple filter is not an
  ambient read; a lint on constants does not see it. The cause is
  removed by finishing the `Coords` migration behind one value
  object (`InstallSelector`), and the lint then bans partial-key
  comparisons so it cannot return.
- **M6. The oracle must be validated.** A wrong model is the broken
  instrument. Every row of the table in §1 becomes a model unit
  case with the known-correct answer, and the differential harness
  must show impl≡impl and model≡model before it reports anything.
- **M7. Dry-run is a law**, not a separate test: `-d` implies
  before == after and the printed plan equals the model's delta.
- **M8. Reproducibility.** A failing exhaustive case prints its
  (shape, world, ctx, argv) and a one-line replay command.
- **M9. Budget.** Default `-t` stays under 15 s. The full
  enumeration is its own lane.
- **M10. No gems.** The mutator is homegrown on Prism (already used
  by `source_digest.rb`; Ruby 3.4.7 ships Prism 1.5.1). Equivalent
  mutants need an annotation grammar, and a test that annotations
  still point at live sites.
- **M11. Staleness and origin are state.** A world key carries
  `record ∈ {ok, changed, missing}` and `origin ∈ {default, pinned}`,
  or `--check-for-updates` and `--upgrade` are unobservable.
- **M12. One world builder.** `fake_install` (DONE, test_helper.rb)
  builds initial worlds; the operation under test runs through
  `Main.main(argv)` so the argument-computing layer -- where every
  `-u` bug lived -- is under the theorem.
- **M13. Process.** The sibling sweep: after a bug in one reader of
  shared state, enumerate every other reader and check each. And: a
  pkgmgr bug fix is not done until the mutant that reproduces it is
  expressible by the mutator and killed by a test.

## 3. The plan, in order

Dependencies force this order: lint → model → refactor-to-model →
laws → exhaustive → mutation → wiring. Sizes are lines of Ruby.

### Step 0 -- the two known instances (DONE partially)

- DONE: `board_for`, `with_target_coords`, `Package#board_bsp`,
  `board_supported?` (commit b31f57253); `NoRealToolchainReads`
  guard and `fake_install` (commit 797515aa4).
- Delete `Layout.board_of`; `Layout` calls `pkgmgr.board_for(arch)`.
  layout.rb gains `require_relative 'package_manager'`.
- `GccPackage#default?`: `ARCH` → `pkgmgr.target_arch` (both reads).
- `show_status_all`: `ARCH.gcc_ver` → `target_arch.gcc_ver`.
- Negative check for each: the sibling-sweep grep in §1 returns only
  the allowlisted sites.

### Step 1 -- the lint (~150 lines)

Files: `scripts/pkgmgr/tests/lint/ambient.rb` (the Prism walker,
shared with the mutator), `scripts/pkgmgr/tests/test_lint_ambient.rb`.

Rules, each a Prism node predicate:

- **R1 ambient constants.** Any `Prism::ConstantReadNode` whose name
  is in `ARCH BOARD DEFAULT_BOARD HOST_VER_GCC` outside the
  allowlist fails, naming `file:line`.
- **R2 partial-key identity.** Any `CallNode` named `==`/`!=`/`eql?`
  whose receiver is a `CallNode` named `arch`, `compiler`,
  `target_arch` or `pkgname` on any object, outside the allowlist,
  fails. (`.ver ==` stays legal: a version alone is a legitimate
  question. `Coords#==` is the identity.) Enabled after step 3.
- **R3 direct scope writes.** Any assignment to `@target_arch`,
  `@target_board`, `@portable_stack` outside `with_target_arch`,
  `with_target_coords`, `with_host_stack`, `host_stack=` fails.

Allowlist: an array of `[file, method]` pairs, each with a one-line
reason. The test asserts every allowlisted method still exists (via
`instance_method`/`method` lookup), so the list cannot rot. Initial
R1 entries: `early_logic.rb` (all), `main.rb#early_checks`,
`#dump_context`, `#set_gcc_tc_ver`, `#check_gcc_tc_ver`,
`#parse_options`, `#read_gcc_ver_defaults`, `package_manager.rb#target_arch`,
`#board_for`, `#current_host_stack`, `layout.rb#vars`.

Self-test: the walker is run on a string containing one violation of
each rule and must report exactly those. A lint that cannot see a
planted violation reports nothing.

Docs: CLAUDE.md gains "the sibling sweep" and "no allowlist entry
without a reason".

### Step 2 -- the model (~400 lines, plus ~150 of cases)

Files under `scripts/pkgmgr/tests/model/`: `world.rb`, `shapes.rb`,
`semantics.rb`, `argv.rb`; cases in `tests/test_model.rb`.

Types (pure, frozen, no I/O, no globals, no `pkgmgr`):

```ruby
Key   = Data.define(:name, :ver, :coords, :record, :origin)
        # coords: the real Coords (pure value object, reused)
        # record: :ok | :changed | :missing     -- .build_inputs vs recipe
        # origin: :default | :pinned            -- .install_origin
World = Set[Key]
Ctx   = Data.define(:arch, :board, :stack)      # what ARCH/BOARD/-a/-H resolve to
Shape = Data.define(:name, :kind, :versions, :default_ver, :deps, :pins,
                    :arch_list, :board_list, :default, :host_world_root,
                    :install_archs)
        # kind: :target | :noarch | :portable | :distro | :compiler |
        #       :stack | :cross_cc
Registry = [Shape]
Request  = Data.define(:mode, :targets, :force, :dry, :arch, :cc, :stack)
        # mode: :install | :uninstall | :upgrade | :clean | :configure |
        #       :list | :check_updates | :installable | :deps | :stacks |
        #       :layout
```

Semantics, one method per rule the implementation has to obey:

```ruby
Model.coords_of(shape, ver, ctx)          # placement: the table in
                                          # docs/package_manager.md, verbatim
Model.select(world, req, ctx)             # the installs an operation names
Model.plan(registry, world, req, ctx)     # closure − installed-at-ctx,
                                          # deps first; pins resolved;
                                          # conflict → error
Model.install(registry, world, req, ctx)  # each planned key added with
                                          # record :ok, origin from req
Model.force_install                       # remove exactly coords_of(name,
                                          # v, ctx) for each install_archs,
                                          # then install
Model.uninstall(world, req, ctx)          # world − select(...)
Model.upgrade / clean / configure
Model.observe_list(world, ctx)            # per package: keys + state
Model.observe_check_updates(registry, world, ctx)  # 0 or 2, names
Model.observe_installable / observe_deps / observe_layout(ctx)
Model.dry(world, req, ctx)                # (world, printed delta)
```

`argv.rb` maps the enumerated grammar (§4) to `Request` with its own
20-line parser, so `Main.parse_options` and `expand_install_all` are
under test rather than shared.

Validation of the model (before it is used as an oracle):

- one unit case per row of §1, with the expected world/answer
  written by hand;
- `Model.select` is total: for every (world, req, ctx) in the
  domain, `select ⊆ world`;
- determinism: two calls, same inputs, same output.

### Step 3 -- finish the identity migration (~300 lines changed)

The model's `select` becomes the implementation's selector.

```ruby
# scripts/pkgmgr/install_selector.rb
InstallSelector = Data.define(:name, :ver, :where)
  # name:  String | :all
  # ver:   Ver    | :all
  # where: CoordsFilter -- machine/env/stack each a String or :any
  def matches?(inst) = name_ok?(inst) && ver_ok?(inst) && where.include?(inst.coords)
```

Changes:

- `PackageManager#uninstall(selector, dry:, force:, except:)`. The
  `(pkg_or_name, dry, force, ver, compiler, arch, coords:)` signature
  and the `all_*` flags go.
- `Main.uninstall_selector(opts) -> InstallSelector`: ONE pure
  function turning `-u PKG[:VER] -a A -c C -f`, `-U ARCH`, `ALL` into
  a selector. Table-tested on its own.
- `force_remove(name, ver)` builds `InstallSelector` from
  `install_archs` + `coords`.
- `find_install(ver)`, `installed?`, `install_prefix` route through a
  selector for `(name, ver, coords(ver))`.
- Convert the other partial-key sites: `package_manager.rb:287, 305,
  387, 441, 615, 1237`; `package.rb:1422`.
- Delete `Layout.board_of` (if not done in step 0).
- Turn on lint R2.

Acceptance: 950+ tests green; `restamp check` on the real tree
reports every install `ok`; the model cases of step 2 pass against
the implementation through `test_cli_matrix`-style runs.

### Step 4 -- the laws (~120 lines)

File: `scripts/pkgmgr/tests/laws.rb`, applied by `run_cli` (moved to
`test_helper.rb`) around every `Main.main` in every test, and by
`with_stubbed_externals` around direct `pkgmgr.*` calls.

```ruby
# before/after are World snapshots of the fake tree
L1  (before ^ after) ⊆ Model.select(before, req, ctx)      # effect ⊆ named
L2  req.dry  ⇒  before == after                            # dry-run
L3  ∀k ∈ after: k.coords == impl.coords(k.name, k.ver) judged at k's scope
L4  ∀k just installed: record == :ok, origin == req's
L5  Main.main(argv) twice ⇒ second run changes nothing     # idempotence (-s, -u)
L6  every install on disk has a .build_inputs (never :missing after -s)
```

`run_cli(*argv, laws: false, because: "...")` is the only opt-out
and the reason is mandatory. Negative check: a test that breaks L1
on purpose (a selector that returns the other install) fails.

### Step 5 -- the exhaustive lane (~350 lines)

Files: `scripts/pkgmgr/tests/exhaustive/{domain,runner}.rb`;
`tests/test_exhaustive.rb` (the sampled default lane); flag
`--exhaustive` on `-t`.

**5.0 measure first.** Time 1,000 in-process `Main.main` runs on a
fake tree with stubs. Planning assumption: 10 ms/case. If it is
30 ms, the bound in 5.1 halves before anything is written.

**5.1 domain.**

- Shapes (catalogue, ~12): target/1 version; target/2 versions;
  target restricted to one arch; target with `board_list`; noarch;
  host `:portable`; host `:distro`; host `:compiler`; host `:stack`;
  `:stack` with a pinned dep; cross-cc; a dep chain a→b→c; a diamond;
  two roots pinning one dep to different versions (conflict); a
  `host_world_root`.
- Candidate keys per shape: versions × coords the shape can occupy
  (for a target package: i386/pc, x86_64/pc, riscv64/qemu-virt,
  riscv64/licheerv-nano) × record ∈ {ok, changed} × origin ∈ {default}
  (pinned only for the shapes with two versions).
  Target/2 versions: 2 × 4 × 2 = 16 candidates.
- Worlds: all subsets of size ≤ 2. For 16 candidates:
  1 + 16 + 120 = 137.
- Contexts: `ARCH ∈ {i386, riscv64}` × `BOARD ∈ {default, other}` ×
  `-a ∈ {none, the other arch}` × `-H ∈ {none, other stack}`,
  invalid combinations pruned: ≈ 8.
- Commands (the grammar, as argv strings):
  `-s X`, `-s X:V`, `-s X -f`, `-s X:V -f`, `-s ALL`, `-s X -a A`,
  `-s X -H S`, `-u X`, `-u X:V`, `-u X -a A`, `-u X -c C`,
  `-u X:V -c C`, `-u ALL`, `-u ALL -f`, `-U A`, `--upgrade`,
  `--clean`, `-C X`, each also with `-d`; queries `-l`, `-l -g ver`,
  `--check-for-updates`, `--list-installable`, `--deps X`, `-L`,
  `--print-layout`. ≈ 45.
- Size: 12 × 137 × 8 × 45 ≈ 590,000 cases. At 10 ms: 98 min serial;
  forked per shape across 12 processes: ~10 min. Too slow for the
  default lane, fine for CI.

**5.2 lanes.**

- default `-t`: all size-≤1 worlds (≈ 12 × 17 × 8 × 45 = 73,000 →
  too many; sample 3,000 with a fixed seed) plus 2,000 seeded size-2
  cases. Budget: ≤ 30 s. Seed printed; `--seed N` reproduces.
- `-t --exhaustive`: everything, forked per shape, in the six
  toolchain workflows.
- `--case ID` replays one case; the failure message prints the ID
  and the argv.

**5.3 one case.**

```
1. reset_pkgmgr!; register the shape's fake packages
2. with_fake_tc: fake_install each key (record → write a matching /
   corrupt .build_inputs; origin → .install_origin)
3. open ctx: with_context(ARCH:, BOARD:), -a/-H come from argv
4. expected_world, expected_out = Model.step / Model.observe
5. rc, out = Main.main(argv) under with_stubbed_externals, laws ON
6. actual_world = snapshot; compare; for queries compare parsed out
```

**5.4 self-test before reporting.** The runner first compares 100
cases impl-vs-impl and model-vs-model and requires equality. A
comparison that cannot find a subject equal to itself reports
nothing.

### Step 6 -- mutation testing (~400 lines)

Files: `scripts/dev/claude/pmmutate` (dispatcher: `sites`, `run`,
`report`, `check-annotations`); `scripts/pkgmgr/tests/mutation/{operators,driver}.rb`.

Operators (Prism node rewrites, exact list):

| # | site | mutant |
|---|---|---|
| 1 | `==` / `!=` / `<` / `<=` / `>` / `>=` | each other |
| 2 | `&&` / `\|\|` | each other |
| 3 | `!x` | `x` |
| 4 | `a && b && c` | drop one conjunct |
| 5 | `return X if cond` / `next if cond` | delete the line |
| 6 | `a \|\| b` | `a` |
| 7 | literal `nil` / `"ALL"` / `:all` in call args | each other |
| 8 | `.select` / `.reject` / `.any?` / `.all?` / `.find` | select↔reject, any↔all |
| 9 | named-call table | `with_target_coords(a,b)`→`with_target_arch(a)`; `board_for(a)`→`BOARD`; `target_arch`→`ARCH`; `pkg_dirname`→`name`; `coords(ver)`→`coords()`; `with_install_context(i){}`→`yield` |
| 10 | `x = expr` → `x = nil` in the scope setters | prev not restored |

Scope (the logic core): `coords.rb`, `install_selector.rb`,
`dep_resolver.rb`, `version_solver.rb`, `build_inputs.rb`,
`layout.rb`, and in `package.rb` / `package_manager.rb` the methods
`coords`, `target_board`, `board_bsp`, `board_supported?`,
`arch_supported?`, `with_install_context`, `build_inputs_state_of`,
`find_install`, `installed?`, `install_prefix`, `install_dir`,
`pkg_dir_at`, `*_get_install_list`, `all_stack_coords`,
`target_arch`, `board_for`, `with_target_arch`,
`with_target_coords`, `with_host_stack`, `current_host_stack`,
`stack_coords`, `uninstall`, `force_remove`, `resolve_install_plan`,
`install`, `resolved_versions_for`, `host_world_names`,
`get_stale_packages`, `get_upgradable_packages`,
`Main.uninstall_selector`, `Main.expand_install_all`,
`Main.select_host_stack`.

Driver: for each site, write the mutated file to a temp dir, run the
suite with that file substituted (a `$LOADED_FEATURES` shim, one
process per mutant, `--filter` to the test files that reference the
mutated method by name plus the laws and the sampled exhaustive lane),
record killed / survived / timeout. Timeout = 3× the clean run.

Equivalent mutants: `# mutation: equivalent -- <reason>` on the site's
line. `pmmutate check-annotations` fails if an annotated line no
longer contains a site.

Acceptance: 0 survivors in scope. The operator table must be able to
express every row of §1 (row 4 is operator 9, row 2 is operator 7,
row 3 is operator 4); a bug the table cannot express gets a new
operator before its fix is committed.

### Step 7 -- wiring and documentation (~½ day)

- `-t`: default lane ≤ 15 s (laws on, sampled exhaustive).
- Six toolchain workflows: `-t --exhaustive`.
- Mutation: `pmmutate run` locally before any commit touching a
  scope file; a weekly `workflow_dispatch` lane.
- `docs/package_manager.md`: section "Correctness guarantees" -- the
  four mechanisms, what each proves, what none of them proves.
- CLAUDE.md: sibling sweep; the mutant rule; allowlist rule.

## 4. What this does and does not prove

Proves, mechanically, on every commit: for the catalogue of shapes,
worlds up to size 2, the enumerated contexts and the enumerated
grammar, the implementation's tree effect and query answers equal the
model's; the implementation reads its inputs only through the owners;
every line of the logic core is covered by a test that fails if it is
wrong.

Does not prove: worlds of size ≥ 3 (raise the bound when a bug ever
appears there -- every bug so far manifested at 2); shapes outside
the catalogue (add one when a package with a new feature appears);
anything about the real recipes or the network.

## 5. Order of work and checkpoints

| step | deliverable | checkpoint |
|---|---|---|
| 0 | three readers fixed, `Layout.board_of` gone | sibling grep returns only owners |
| 1 | lint + allowlist + CLAUDE.md rules | planted violations detected; suite green |
| 2 | model + §1 cases | cases pass; select total; deterministic |
| 3 | `InstallSelector`; uninstall/force_remove/find_install on it; R2 on | suite green; `restamp check` all ok; real `-u`/`-f` on two stacks, two boards, two versions each remove one |
| 4 | laws in `run_cli` | planted L1 violation fails; suite green; ≤ 15 s |
| 5 | exhaustive domain + runner + lanes | self-test equal; sampled lane ≤ 30 s; full lane green in CI |
| 6 | `pmmutate` + operators + annotations | 0 survivors in scope; every §1 row expressible |
| 7 | CI + docs | workflows green with `--exhaustive` |
