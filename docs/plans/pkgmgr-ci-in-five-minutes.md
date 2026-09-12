# The package manager's CI in five minutes, every time

Goal, as stated: the package manager's own tests -- the ones that
build nothing real -- pass on CI in five minutes or less,
deterministically, every run. This is what they cost today, why one
job was not deterministic, what was measured about the redundancy of
the exhaustive lane, and a proposal with the trade-offs left open.

## Where the time goes today

Two jobs, both on a 4-vCPU `ubuntu-22.04` runner in a
`debian:stable-slim` container. Numbers from runs 34043942317 and
34159480348 on `exp-work`.

| job        | what                                  | wall    |
|------------|---------------------------------------|---------|
| `tests`    | unit suite (1,169 tests)              | 25 s    |
|            | of which the 1,000-case sampled lane  | 16 s    |
|            | the full exhaustive lane              | 59 min  |
| `mutation` | clean suite, then 427 mutants x suite | 80 min  |

The exhaustive lane is 368,502 cases at ~28 ms each on CI (6 ms
here), forked one process per shape, so the wall time is the biggest
shape: `target_2v`, 138,990 cases, 59 minutes alone. The marks commit
(`8f688a0e`) multiplied the lane by 3.3: every candidate install now
comes in two marks, and worlds are pairs of candidates.

The mutation job is 427 mutants, each judged by a full suite run:
median 41 s under four parallel workers, so 427 x 41 / 4 = 73 minutes
plus the clean run. The per-mutant budget is five times the clean
suite, 120 s at least.

## Why the mutation job was not deterministic

Four runs, 10, 1, 21 and 3 timeouts, always mutants of the same
lines: `own_host_supported?`, `supported?`, `install_archs`,
`install_dir`, `with_host_stack` -- everything that decides whether a
package is supported here or where an install lives. None of them
timed out locally under the driver's exact conditions.

A watchdog thread (`PKGMGR_WATCHDOG`, commit `ed36aa85`) named it:

    system_deps.rb:357:in 'system'       Env#run
    system_deps.rb:184:in 'run'          CargoInstaller#run
    system_deps.rb:547:in 'run_installers'

Under such a mutant, `test_a_world_package_is_refused_at_the_door`
(`-s host_qemu` on a host stubbed to macOS, real packages) is no
longer refused; the plan reaches `SystemDeps.check_plan`, and the
harness had no stub for that machine. On CI cargo is present, cargo-c
is not, and `RUNNING_IN_CI` makes the remediation unattended, so the
suite ran `cargo install cargo-c` -- two minutes of compiling -- on
every runner that judged those mutants. Here cargo-c is installed, so
the same mutant died in thirteen seconds. Which mutants crossed the
120 s line depended on how loaded the runner was; that was the whole
of the non-determinism.

Fixed in `c14ea544`: `SystemDeps` holds its machine as one object
with a seam, `with_stubbed_externals` hands it a machine on which
every tool is present, every package installed and nothing runs, and
the real `Env` refuses to run anything inside a test. Run 34159849245
-- every `package.rb` mutant after the fix -- is the verification:
98 killed, none timed out, the slowest kill 47 s against a 130 s
budget, where the same set had 21 timeouts before.

What remains non-deterministic is only the budget itself: five times
a clean run measured on the same runner, under the same four workers.
With no test able to reach the machine, a timeout is a defect in the
code -- and the watchdog now says where.

## The redundancy of the exhaustive lane, measured

The lane is a cartesian product: worlds x contexts x command lines.
The question was how much of it exercises the same behaviour.

**By the model.** Running only the model over all 368,502 cases
takes 9 seconds. Bucketing each case by the model's verdict -- the
command line, the return code, the message, and the exact set of
installs added and removed -- gives **6,985 distinct behaviours**.
Everything else is the same pattern on a different world: 53x.

| shape        | cases   | behaviours |
|--------------|---------|------------|
| target_2v    | 138,990 | 2,849      |
| stack        |  83,880 | 1,229      |
| stack_pin    |  61,776 |   875      |
| conflict     |  21,432 |   539      |
| diamond      |  19,602 |   669      |
| target       |  13,176 |   524      |
| the other 9  |  29,646 | 1,300      |

**By the implementation.** Line and branch coverage of
`scripts/pkgmgr/*.rb` (tests excluded), the full lane against one
representative per bucket (the first case in enumeration order):

| set                                          | cases   | covered |
|----------------------------------------------|---------|---------|
| the full lane                                | 368,502 | 4,218   |
| one per behaviour                            |   8,752 | 4,180   |
| ...plus dry runs keyed by their non-dry twin |         |         |
|    and queries keyed by the world's shape    |  17,292 | 4,212   |

The 38 lines the first reduction missed were all of two kinds: dry
runs (whose verdict is "unchanged" whatever they would have done) and
`-l` formatting for worlds with several installs of one package. The
refined key recovers all but six: the `--rebuild` "left as it is"
message for an install at a board the package does not build for,
one branch of the default install's listing, the "it is installed
at" hint after an uninstall that matched nothing, and the cycle
fallback in autoremove's ordering, which no world can reach. Those
are message paths the model does not model; each can be given a
bucket of its own by adding the impl's message class to the key, or
left to the full lane.

The 17,292 representatives run in 76 s here in one process; at CI's
4.5x that is ~6 minutes single-process, and forked by shape the wall
is set by `target_2v`'s share, about two minutes.

## Proposal

**1. The lane is exhaustive over behaviours, not over worlds.**
`-t --exhaustive` enumerates every case as today, runs the model
over all of them (seconds), and runs the implementation on one
representative per behaviour: the first case of each bucket, so the
set is deterministic and stable across runs. The full product stays
available as `--exhaustive --full`, for a nightly workflow and for
whoever wants it, and the nightly run also asserts that the reduced
lane's coverage equals the full lane's minus the listed exceptions --
so a new code path that only the product reaches is noticed rather
than lost. Cost on CI: ~2 minutes, down from 59.

This is also the answer to "the same pattern over and over": a case
whose model verdict matches an earlier one is, by construction, the
same pattern. What it cannot see is a code path the model does not
distinguish; the coverage assertion is the guard for that.

**2. The sampled lane in `-t` becomes a slice of the same set.** The
1,000-case random sample is what kills most mutants; a deterministic
slice of the representatives (say the first 1,000, spread across
shapes as today) does the same job without a seed, and every local
run judges the same cases.

**3. Mutation is sharded.** 427 mutants at ~41 s each is 4.9
CPU-hours however it is cut; five minutes of wall needs ~15 runners
of 4 workers. `--shard i/n` on the driver partitions the mutant list
deterministically; a matrix job runs the shards in parallel and a
final step sums the verdicts. GitHub allows 20 concurrent jobs on a
public repository, so 15 shards fit, at the price of 15 container
setups (~1 min each, in parallel).

The cheaper alternative is to shrink what a mutant runs: the suite is
25 s on CI of which 16 s is the sampled lane; a 300-case slice would
make a mutant ~15 s and 8 shards enough -- but killing power has to
be re-measured (today: zero survivors), and that is a trade-off
rather than a free win.

**4. Timeouts stay a defect.** The budget rule stays (5x clean, 120 s
floor); with no road to the machine left, a timeout is a walk without
a bound in the code, and the report now carries the stack.

## Expected result

| job        | today  | proposed                                   |
|------------|--------|--------------------------------------------|
| `tests`    | 60 min | ~3 min: setup, unit suite, behaviour lane  |
| `mutation` | 80 min | ~5 min wall with 15 shards; ~10 with 8     |
| nightly    | --     | full product + coverage equality, ~1 h     |

## Trade-offs left open

- One representative per behaviour, or two? Two doubles the lane
  (~4 min) and catches an implementation whose first case of a
  bucket happens to agree while a later one would not; the coverage
  data says one is enough for the code as it is.
- The six message paths: model them (the key grows the impl's
  message class), or leave them to the nightly full lane.
- Mutation: 15 shards for five minutes, or 8 shards and ten, or a
  smaller per-mutant suite with the killing power re-measured.
- Whether the nightly full lane exists at all. Without it, the
  coverage guard has nothing to compare against, and the reduction
  is trusted on today's measurement alone.
