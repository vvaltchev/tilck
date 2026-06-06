# Plan: add a 4th test type "unit" to `run_all_tests`

## Goal

Make `run_all_tests` list and run the host-side googletest unit tests
(the `gtests` binary) as a first-class test type, alongside `selftest`,
`shellcmd`, and `interactive`. So that `run_all_tests` truly runs *all*
the tests, and `-T unit`, `-l`, `-f`, `-t` all work for unit tests too.

## How the runner is wired today (pointers)

- `tests/runners/lib/utils.py:15` — `TEST_TYPES` / `TEST_TYPES_PRETTY`
  are the source of truth. Almost everything else in `run_all_tests`
  is derived from these two by index-aligned `enumerate()`.
- `tests/runners/run_all_tests`:
  - `load_tests_func_by_type_list` (line 59) — per-type loader names,
    index-aligned with `TEST_TYPES`. `load_all_tests()` (132) calls each.
  - Each loader returns `[[name, timeout], ...]` and sets
    `test_runners[type]` to the inner-runner path. See
    `load_list_of_shell_cmd_tests` (173), `load_list_of_interactive_tests`
    (137) — the latter is the model for graceful "feature absent → []".
  - `internal_single_test_runner_thread` (253) spawns
    `[test_runners[type], type, name, str(timeout), kvm_version]` and
    maps the child's exit code through the `Fail` enum
    (`lib/utils.py:22`): `0` == pass, anything else == fail.
  - `compact_pack_tests` (585) rewrites `tests_by_type` for `-c`.
- `other/cmake/gen_config_pre.cmake:40-53` — `smart_config_file`
  expands the `@CMAKE_*@` placeholders in each runner script into
  `build/st/`. A new runner script must be added here.
- The `gtests` binary lives at `@CMAKE_BINARY_DIR@/gtests` (==
  `BUILD_DIR/gtests`); built only when `GTEST_INSTALL_DIR` exists,
  and `EXCLUDE_FROM_ALL` (so `make gtests`, not plain `make`).

## gtest behavior we rely on (verified)

- `gtests --gtest_list_tests` prints suite lines (no indent, end with
  `.`) followed by indented case lines. Parametric instantiations
  appear as separate suites `Prefix/Suite.` with cases `case/0`,
  `case/1`, ... and a trailing `# GetParam() = ...` comment.
- A folded entry `Prefix/Suite.case` is run with the combined filter
  `Prefix/Suite.case:Prefix/Suite.case/*` — the first pattern matches
  a plain (non-param) test, the second matches every param instance.
  Verified: combined filter runs all 4 `do_nothing/mem_regions_test`
  instances; on a non-param test only the plain pattern fires.
- Exit code: `0` on success, nonzero on any failing test.
- **`DISABLED_` tests** (name or suite prefixed `DISABLED_`) are *not*
  run by default; `--gtest_also_run_disabled_tests` is required.
  Without it, a filter naming only a disabled test runs 0 tests and
  exits `0`. Verified on `fat32.DISABLED_dumpinfo`: skipped without
  the flag, runs (`[ OK ]`, exit 0) with it.
- **Gotcha:** a filter that matches *nothing* still exits `0` and
  prints `Running 0 tests from 0 test suites` / `did not match any
  test`. The wrapper must guard against this false pass (it can only
  happen on a parser/stale-binary bug, since names come from
  `--gtest_list_tests`).

## Folding rule

Parse `--gtest_list_tests`:
- A line with no leading space and a trailing `.` sets the current
  suite = line without the `.`.
- An indented line is a case: strip whitespace, strip a trailing
  ` # ...` comment, take the part before the first `/` as the base
  name (drops the param index `/N`).
- Emit `suite + "." + base`, de-duplicated per (suite, base).
- Mark an entry **manual** (disabled) when `base` starts with
  `DISABLED_`, or the suite's class component
  (`suite.rsplit('/', 1)[-1]`) starts with `DISABLED_`. These are the
  unit-test equivalent of "manual" self-tests: always *listed* (shown
  as `<manual>` under `-L`/`-l`), but only *run* when `-a`
  (`--allow-manual`) is passed — exactly the existing manual
  mechanism (`does_test_match_criteria`, `run_all_tests:341-346`).

Result on the current tree: 272 folded entries — 261 regular + 11
manual/disabled (the `avl_bintree.DISABLED_*` benchmarks,
`fat32.DISABLED_dumpinfo`, `runqueue_bench.DISABLED_summary`).

## Changes

### 1. `tests/runners/lib/utils.py` — register the type

Add `unit` as the **first** type (fastest, host-only → fail-fast,
quickest feedback):

```python
TEST_TYPES        = ['unit', 'selftest', 'shellcmd', 'interactive']
TEST_TYPES_PRETTY = ['Unit tests', 'Self tests',
                     'Shell cmd tests', 'Interactive tests']
```

### 2. `tests/runners/run_all_tests` — loader + list func name

- Prepend the loader name, keeping index alignment with `TEST_TYPES`:

  ```python
  load_tests_func_by_type_list = [
     'load_list_of_unit_tests',
     'load_list_of_kernel_self_tests',
     'load_list_of_shell_cmd_tests',
     'load_list_of_interactive_tests',
  ]
  ```

- Add `GTESTS_FILE = os.path.join(BUILD_DIR, 'gtests')` near the other
  CMake-derived paths (no new CMake var needed; `BUILD_DIR` already ==
  `@CMAKE_BINARY_DIR@`).

- Add `load_list_of_unit_tests()`:
  - `subprocess.check_output([GTESTS_FILE, '--gtest_list_tests'])`,
    wrapped in `try/except (FileNotFoundError, CalledProcessError)` →
    return `[]` (mirror `load_list_of_interactive_tests`: an unbuilt /
    unavailable `gtests` is "feature absent", not an error).
  - Apply the folding rule above. A regular entry gets timeout
    `TIMEOUTS['short']` (12 s — host tests are sub-second, but a hung
    test still needs a bound; 12 ≤ default `med`=36 so unit tests run
    by default). A disabled entry gets `MANUAL_TEST_TIMEOUT`, which is
    what flags it `<manual>` and gates it behind `-a` — same as manual
    self-tests.
  - `test_runners["unit"] = "@CMAKE_BINARY_DIR@/st/run_unit_test"`.
  - `return sorted(result)`.

### 3. `tests/runners/run_unit_test` — new inner runner (~50 lines)

A dedicated wrapper, modeled on `single_test_run` but with **no QEMU**.
Rationale for a separate script (not inlining into the generic
spawner): keeps `internal_single_test_runner_thread` generic, and lets
us translate gtest's exit codes into the project's `Fail` enum
(gtest's failure exit `1` would otherwise collide with
`Fail.invalid_args`).

- argv: `unit <name> <timeout> [qemu_kvm_version]` (the kvm arg is
  accepted and ignored, like `single_test_run`).
- `name` may contain spaces (compact mode packs several names into
  one argv element). Split on whitespace → `names`.
- Build the filter: for each n, two patterns `n` and `n/*`, joined by
  `:`. If `names == ['runall']` (or empty) → no `--gtest_filter` (run
  the whole binary).
- If any selected name is disabled (`'DISABLED_' in n`), add
  `--gtest_also_run_disabled_tests` (otherwise gtest runs 0 tests and
  the false-pass guard below would report a spurious failure). The
  flag is only ever set when the explicit filter names a disabled
  test, so non-disabled tests are unaffected. In `runall` mode the
  flag is *not* set, so the whole-binary compact run skips disabled
  tests — consistent with how compact `runall` skips manual
  self-tests.
- `subprocess.run([GTESTS_FILE, ...], timeout=timeout, capture)`,
  print output via `direct_print`. Exit mapping:
  - missing binary → `Fail.invalid_build_config`
  - `TimeoutExpired` → `Fail.timeout`
  - child returncode != 0 → `Fail.some_tests_failed`
  - output shows `Running 0 tests` / `did not match any test`
    → `Fail.no_tests_matching` (false-pass guard)
  - else → `Fail.success`

### 4. `other/cmake/gen_config_pre.cmake` — configure the new script

Add a `smart_config_file` block mirroring lines 40-53:

```cmake
smart_config_file(
   ${CMAKE_SOURCE_DIR}/tests/runners/run_unit_test
   ${CMAKE_BINARY_DIR}/st/run_unit_test
)
```

### 5. `tests/runners/run_all_tests` — compact mode (`-c`)

`compact_pack_tests` (585) packs in-VM types into a single boot. Unit
tests have no boot, but spawning ~272 host processes per `-c` run is
wasteful, so pack them into a single `gtests` invocation:

- Without `-f`: `tests_by_type['unit'] = [['runall', ALL_TESTS_TIMEOUT]]`
  (the wrapper runs the whole binary once).
- With `-f`: the existing per-type packing loop already produces
  `packed_name = names_str` (space-joined) for non-shellcmd types — the
  unit wrapper consumes that directly and builds one combined filter.
  Unit is *not* subject to the selftest `KERNEL_SELFTESTS` guard, so it
  participates in the loop normally (no special-case needed beyond not
  treating it like shellcmd's `runall ` prefix — it already isn't).

  The `COMPACT_FILTER_MAX` cap (10) still applies; for unit that's
  unnecessarily small but harmless. *Optional:* exempt unit from the
  cap since one host process handles any count — decide at impl time.

### 6. (Optional) skip KVM detection for unit-only runs

`main()` calls `detect_kvm()` unconditionally (695). It is non-fatal
(just warns when KVM is absent — `lib/detect_kvm.py:34`), so unit-only
runs already work on hosts without KVM (e.g. macOS). Optionally skip
the call when `args.test_type == 'unit'` to suppress the spurious
"qemu-kvm not found" warning. Low priority.

### 7. Docs

- `docs/testing.md`: note that `run_all_tests` now also runs unit
  tests, e.g. `run_all_tests -T unit [-l]`.
- `CLAUDE.md` Testing section: add `-T unit` to the examples.

## Verification

- `./build/st/run_all_tests -T unit -L` lists all 272 folded entries,
  with the 11 disabled ones shown as `<manual>`; param suites
  collapsed to one `…/Suite.case` each.
- `./build/st/run_all_tests -T unit` runs the 261 regular ones (manual
  excluded); all pass; summary shows `Unit tests passed: 261/261`.
- `./build/st/run_all_tests -T unit -a` additionally runs the 11
  disabled tests (wrapper passes `--gtest_also_run_disabled_tests`).
- `./build/st/run_all_tests -T unit -f 'console_test.*'` filters.
- `./build/st/run_all_tests -T unit -c` → single `gtests` process.
- `./build/st/run_all_tests -l` (all types) includes the unit block.
- A build without `gtests` built → unit loader returns `[]`, no crash.
- Sanity: temporarily break one unit test → it's reported FAILED and
  the run exits nonzero (confirms exit-code mapping + false-pass
  guard).

## Out of scope / non-goals

- No change to how `gtests` itself is built or to `tests/unit/`.
- Coverage (gcda) handling: unit-test coverage is host gcov, a
  separate path from the in-VM gcda dump; not wired here.
