# Migrating Tilck CI from Azure Pipelines to GitHub Actions

Status: **IMPLEMENTED** on `exp-work`. Nine workflow files in
`.github/workflows/`: `ci-i386`, `ci-riscv64`, `ci-x86_64`, and one
`ci-tc-<distro>-<arch>` per toolchain case — all validated with actionlint.
Earlier fixes also landed: the riscv64 `gcc_13_3` matrix bug and the CodeCov
script modernization (§5.4). Decisions taken: push-only triggers +
`workflow_dispatch` (Azure has no PR builds — O2); the toolchain stays one file
per case, NOT a matrix, so each keeps its own status badge (GHA has no per-job
badge — O3). **One deviation from the plan below:** coverage uploads via
`generate_kernel_coverage_report --codecov` + the `CODECOV_TOKEN` secret (the
flow proven on Azure), *not* the `codecov-action` in §5.2/§6. The
`.github/workflows/` files are authoritative.
Scope: the 9 YAML files in `other/ci/` + the CodeCov integration.

---

## 0. Short answer to the two questions

**Q: Is a full conversion even possible, or does GitHub Actions (GHA)
lack features used here?**

Yes — **everything in `other/ci/` maps 1:1 onto GitHub Actions**. Nothing
in these pipelines uses an Azure-only capability. The concepts used are:
container jobs, a host VM image, per-job env vars, a build matrix, plain
shell steps, and branch/path trigger filters — all of which GHA supports.
There are a handful of *spelling* differences (not capability gaps) that
the migration has to handle deliberately; they are enumerated in §3.

**Q: Will the CodeCov integration keep working?**

Yes. Tilck already uploads with a Codecov **upload token** (kept today as a
secret Azure pipeline variable, so it isn't visible in the YAML). The migration
keeps exactly that: add the token as a **`CODECOV_TOKEN` GitHub Actions secret**
and pass it to the upload step. The one genuinely stale piece is mechanical: the
in-repo script downloads the **deprecated `uploader.codecov.io` binary**, which
CodeCov has superseded by the **Codecov CLI**. §5 covers the fix — use the
official `codecov/codecov-action@v5` (which wraps that CLI) in the GHA jobs, and
the script's local `--codecov` path is modernized to the same CLI. (Token-free
options — tokenless for public repos, or OIDC — exist but Tilck keeps the token;
see §5.)

---

## 1. Inventory of the current Azure pipelines

Each file in `other/ci/` is registered in Azure DevOps as a **separate
pipeline** (definition IDs 14–24, per the README badges). GHA instead
**auto-discovers** every `*.yml` under `.github/workflows/` — no manual
registration.

| # | File | Registered as | Container | Host image | What it does |
|---|------|---------------|-----------|------------|--------------|
| 1 | `i386.yml` | Tilck i386 (id 15) | `vvaltchev/tilck-i386:v7` | ubuntu-24.04 | 3 jobs: `interactive`, `gcc_13_3` (7-leg matrix), `coverage` (uploads to CodeCov) |
| 2 | `riscv64.yml` | Tilck riscv64 (id 14) | `vvaltchev/tilck-riscv64:v7` | ubuntu-24.04 | 3 jobs: `gcc_12_4` (7-leg matrix), `gcc_13_3` (matrix — see note), `coverage` (uploads) |
| 3 | `x86_64.yml` | Tilck x86_64 (id 16) | `vvaltchev/tilck-x86_64:v7` | ubuntu-24.04 | 1 job: build only, no tests |
| 4 | `tc-debian-i386.yml` | Toolchain/debian i386 (id 19) | `vvaltchev/debian:v5` | ubuntu-22.04 | toolchain build + full test run |
| 5 | `tc-debian-riscv64.yml` | Toolchain/debian riscv64 (id 20) | `vvaltchev/debian:v5` | ubuntu-22.04 | toolchain build (fewer pkgs) + tests |
| 6 | `tc-fedora-i386.yml` | Toolchain/fedora i386 (id 21) | `vvaltchev/fedora:v4` | ubuntu-22.04 | toolchain build + tests |
| 7 | `tc-fedora-riscv64.yml` | Toolchain/fedora riscv64 (id 22) | `vvaltchev/fedora:v4` | ubuntu-22.04 | toolchain build (fewer pkgs) + tests |
| 8 | `tc-arch-i386.yml` | Toolchain/arch i386 (id 23) | `vvaltchev/archlinux:v7` | ubuntu-22.04 | toolchain build (no micropython) + tests |
| 9 | `tc-arch-riscv64.yml` | Toolchain/arch riscv64 (id 24) | `vvaltchev/archlinux:v7` | ubuntu-22.04 | toolchain build (fewer pkgs) + tests |

Notes captured while reading the files:

- **`riscv64.yml` `gcc_13_3` job has a latent bug.** It declares the same
  7-leg `GEN` matrix as `gcc_12_4`, but its build step runs
  `./scripts/cmake_run` and *never references `$(GEN)`*. So all 7 legs build
  the identical default config 7×. The migration should **not** silently
  replicate a broken matrix — decide (§8, open question **O1**) whether to
  (a) drop the matrix to a single job, or (b) fix the step to
  `./scripts/build_generators/$(GEN)` like `gcc_12_4`. This is a pre-existing
  issue, independent of the platform move.
- The 6 toolchain files are near-identical; they differ only by
  `container:` image, `ARCH`, and the *package list* installed (i386 builds
  more packages; `arch-i386` omits `micropython`; all riscv64 legs share one
  small package set). This is a natural candidate for consolidation (§4).
- The Python test runner already treats "any CI" uniformly: `IN_ANY_CI` is
  true whenever `RUNNING_IN_CI=1` (see `tests/runners/lib/env.py:22`), which
  every pipeline sets. So **the runner needs no changes** — KVM is correctly
  skipped and coverage dumping is wired the same way regardless of provider.
  (Optional cleanup in §7.)

---

## 2. Azure → GitHub Actions concept map

| Azure Pipelines | GitHub Actions | Notes |
|-----------------|----------------|-------|
| One `*.yml` per registered pipeline | One `*.yml` per file in `.github/workflows/` | Auto-discovered; no UI registration |
| `trigger.branches.exclude` | `on.push.branches-ignore` | Same glob style (`readme*`, `temp-*`, `exp-*`) |
| `trigger.paths.include` + `.exclude` | `on.<event>.paths` with `!` negations | Combined into one ordered list (§3.1) |
| (no `pr:` block) | `on.pull_request` | New, opt-in (§3.4) |
| `pool.vmImage: ubuntu-24.04` | `runs-on: ubuntu-24.04` | Same image labels exist |
| `container: 'img'` | `container: img` (job-level) | Same Docker Hub ref |
| `variables:` (pipeline/job) | `env:` (workflow/job) | Folded `>` scalar works the same |
| `strategy.matrix.<name>: {GEN: ...}` | `strategy.matrix.gen: [...]` | Add `fail-fast: false` (§3.2) |
| `steps: - script:` / `displayName:` | `steps: - run:` / `name:` | Direct rename |
| (implicit checkout) | `- uses: actions/checkout@v4` | **Must be added explicitly** (§3.3) |
| CodeCov via in-repo script + secret token | `codecov/codecov-action@v5` + `CODECOV_TOKEN` secret | §5 |

Conclusion: no missing primitives. The work is mechanical translation plus
the six deliberate decisions in §3 and the CodeCov change in §5.

---

## 3. The genuine differences (and how each is handled)

### 3.1 Path filters: `include` + `exclude` → one ordered list with `!`

Azure lets you specify `include` and `exclude` path lists side by side. GHA
does **not** allow `paths` and `paths-ignore` on the same event, but it
**does** support negation inside a single `paths:` list, evaluated in order
(a later `!pattern` removes an earlier match). That expresses the same
intent. Example for i386:

```yaml
on:
  push:
    branches-ignore: [readme*, temp-*, exp-*]
    paths:
      - '.github/workflows/ci-i386.yml'   # was other/ci/i386.yml
      - 'other/busybox.config'
      - 'kernel/**'
      - 'modules/**'
      - 'include/**'
      - 'common/**'
      - 'boot/**'
      - 'tests/**'
      - 'userapps/**'
      - 'scripts/**'
      - 'config/**'
      - '!README.md'
      - '!docs/**'
      - '!scripts/configurator/**'
      - '!kernel/arch/riscv/**'
      - '!kernel/arch/riscv64/**'
      - '!include/tilck/kernel/arch/riscv/**'
      - '!include/tilck/common/arch/riscv/**'
```

Two things to remember:
- The trigger path that referenced the pipeline file itself
  (`other/ci/i386.yml`) must be updated to the new workflow path
  (`.github/workflows/ci-i386.yml`).
- Semantics are equivalent for this repo's patterns (broad includes minus a
  few subtrees). Edge-case behavior around negation differs slightly from
  Azure only in constructs Tilck doesn't use.

### 3.2 Matrix: named legs → list, and `fail-fast: false`

Azure matrices don't cancel sibling legs on failure. GHA defaults to
`fail-fast: true` (one failure cancels the rest). To preserve today's
behavior, set `fail-fast: false`:

```yaml
strategy:
  fail-fast: false
  matrix:
    gen: [gcc, gcc_rel, gcc_fast_rel, gcc_nocow,
          gcc_no_nested_irq_tracking, minimal, gcc_small_offt]
```

and reference it as `./scripts/build_generators/${{ matrix.gen }}`.

### 3.3 Checkout is explicit

Azure auto-checks-out the repo as an implicit first step. GHA does not — add
`- uses: actions/checkout@v4` as the first step of every job. Default depth
(shallow, `fetch-depth: 1`) is sufficient: the only build-time git use is
`scripts/build_scripts/get_commit_hash` → `git rev-parse --short=8 HEAD`,
which needs just the tip commit. No `git describe`/tags anywhere, so we do
**not** need `fetch-depth: 0`.

### 3.4 PR triggers (new, recommended)

The Azure files define only `trigger:` (CI/push), no `pr:` block. GHA can add
`on.pull_request` so forks/branches get validated before merge. This is
**new behavior** — flag it for approval (open question **O2**). If added,
mirror the same `paths:` list and target `branches: [master]`.

### 3.5 Registration & file naming

9 Azure definitions → 9 workflow files (§4). Because discovery is
by directory, the migration is "add files under `.github/workflows/`",
then disable the Azure pipelines once green.

### 3.6 Docker Hub pull rate limits (operational)

All six images are public on Docker Hub. GHA's shared runners pull
anonymously and can hit Docker Hub's anonymous rate limit on busy days.
Not a blocker; if it bites, mirror the images to GHCR
(`ghcr.io/vvaltchev/...`) and pull from there. Out of scope for the initial
port.

---

## 4. Proposed `.github/workflows/` layout

**9 workflow files** — one per current Azure pipeline. A matrix would be
tidier, but GitHub Actions badges are **per workflow file**, not per job or per
matrix leg, and the README wants one status badge per case (so a single
distro/arch failure doesn't red the whole toolchain). Separate files are the
only way to keep separate badges.

```
.github/workflows/
  ci-i386.yml             # kernel i386   (interactive, build-matrix, coverage)
  ci-riscv64.yml          # kernel riscv64 (gcc_12_4, gcc_13_3, coverage)
  ci-x86_64.yml           # kernel x86_64  (build only)
  ci-tc-debian-i386.yml     # toolchain: one file (= one badge) per case
  ci-tc-debian-riscv64.yml
  ci-tc-fedora-i386.yml
  ci-tc-fedora-riscv64.yml
  ci-tc-arch-i386.yml
  ci-tc-arch-riscv64.yml
```

---

## 5. CodeCov — detailed plan

### 5.1 How it works today

- Both `generate_kernel_coverage_report` and `generate_test_coverage_report`
  are generated from the **same** template
  `scripts/templates/generate_coverage_report` (see `kernel/arch/CMakeLists.txt`
  and `tests/unit/CMakeLists.txt`), differing only by `BUILD_DIR`.
- The i386 `coverage` job generates unit-test coverage, then runs
  `generate_kernel_coverage_report --codecov`. Historically this downloaded
  `https://uploader.codecov.io/latest/linux/codecov` and ran `codecov -X gcov`;
  despite the tree-search it uploaded only `build/coverage.info` (the CI logs
  show `build/tests/unit/coverage.info` is *found but excluded* by Codecov's
  default test-path filtering).
- riscv64's `coverage` job produces only the kernel `build/coverage.info`
  (no gtests there).
- It authenticates with a Codecov **upload token**, provided via the
  `CODECOV_TOKEN` env var (set as a secret CI variable — in Azure, a secret
  pipeline variable, so it isn't visible in the YAML). The template passes it
  through and warns if it's unset.

The only genuinely stale piece is the uploader: the binary at
`uploader.codecov.io` is CodeCov's **deprecated** uploader, which they no longer
add features to and have superseded by the **Codecov CLI** (`codecovcli`). The
token stays; only the uploader changes.

### 5.2 Recommended approach for GHA — the official action

In the coverage jobs, **generate** the `.info` files (run the report scripts
*without* `--codecov`, which just produces `coverage.info`), then upload with
the maintained action:

```yaml
      - name: Gen unit-test coverage report
        run: ./build/tests/unit/scripts/generate_test_coverage_report
      - name: Run system tests
        run: ./build/st/run_all_tests -c
      - name: Gen kernel gcov report            # no --codecov: just build coverage.info
        run: ./build/scripts/generate_kernel_coverage_report
      - name: Upload coverage to Codecov
        uses: codecov/codecov-action@v5
        with:
          token: ${{ secrets.CODECOV_TOKEN }}
          files: ./build/coverage.info          # kernel report (see §5.1)
          disable_search: true                  # upload exactly this file
          fail_ci_if_error: false               # keep today's non-blocking behavior
```

For riscv64, list only `./build/coverage.info`.

Why the action over the old in-repo script path in CI:
- Uses a supported uploader (the action wraps the current Codecov CLI; the old
  `uploader.codecov.io` binary is retired).
- Explicit file list replaces fragile tree-search behavior.
- Auto-detects commit/branch/PR context on GHA; built-in retries + binary
  integrity checks. Token comes from the `CODECOV_TOKEN` secret.

### 5.3 Authentication — the `CODECOV_TOKEN` secret

Tilck uploads with a Codecov **upload token** (today an Azure secret pipeline
variable). The migration keeps this:

- Add the same token as a GitHub Actions secret **`CODECOV_TOKEN`** (repo →
  Settings → Secrets and variables → Actions), and pass it to the action as
  `token: ${{ secrets.CODECOV_TOKEN }}` (shown above). It's an upload-only
  token, but keeping it a secret is still the tidy default.
- `.codecov.yml` at repo root is provider-agnostic and needs **no change**.

Token-free alternatives (not used here, noted for completeness): **tokenless**
for public repos requires flipping the org-level *Global Upload Token* setting
to "not required" (existing orgs default to *required*, and pushes to a
protected branch like `master` still need it otherwise); **OIDC**
(`use_oidc: true` + `permissions: id-token: write`) authenticates via GitHub's
identity with no token. Tilck stays with the token.

### 5.4 The local `--codecov` path — modernized

`docs/coverage.md` documents `generate_kernel_coverage_report --codecov` for
local/manual use. That path is **modernized as part of this work**: the template
`scripts/templates/generate_coverage_report` now downloads the **Codecov CLI**
(`https://cli.codecov.io/latest/linux/codecov`) and runs
`codecov upload-process --disable-search -f coverage.info` (passing
`-t $CODECOV_TOKEN` when the var is set — as it is in CI), instead of the
deprecated `uploader.codecov.io` binary + `codecov -X gcov`. Each build dir
uploads its own `coverage.info`, and it works from any CI provider. Open
question **O4** is thus resolved.

---

## 6. Worked example — `ci-i386.yml`

Full translation of `other/ci/i386.yml` (3 jobs). Illustrates every pattern
above; the other kernel workflows follow the same shape.

```yaml
name: Tilck i386

on:
  push:
    branches-ignore: [readme*, temp-*, exp-*]
    paths:
      - '.github/workflows/ci-i386.yml'
      - 'other/busybox.config'
      - 'kernel/**'
      - 'modules/**'
      - 'include/**'
      - 'common/**'
      - 'boot/**'
      - 'tests/**'
      - 'userapps/**'
      - 'scripts/**'
      - 'config/**'
      - '!README.md'
      - '!docs/**'
      - '!scripts/configurator/**'
      - '!kernel/arch/riscv/**'
      - '!kernel/arch/riscv64/**'
      - '!include/tilck/kernel/arch/riscv/**'
      - '!include/tilck/common/arch/riscv/**'
  # pull_request: { branches: [master], paths: [ ...same... ] }   # see O2

# Optional: cancel superseded runs of the same ref
concurrency:
  group: i386-${{ github.ref }}
  cancel-in-progress: true

env:
  RUNNING_IN_CI: 1
  TCROOT: /tc
  GTEST_SHUFFLE: 0
  TILCK_NO_LOGO: 1
  GCC_TC_VER: 13.3.0

jobs:

  interactive:
    runs-on: ubuntu-24.04
    container: vvaltchev/tilck-i386:v7
    env:
      CMAKE_ARGS: >-
        -DEXTRA_VIM=1 -DKRN_FB_CONSOLE_BANNER=0
        -DKRN_FB_CONSOLE_CURSOR_BLINK=0 -DBOOT_INTERACTIVE=0
        -DPREFERRED_GFX_MODE_W=640 -DPREFERRED_GFX_MODE_H=480
        -DKRN_KMALLOC_FREE_MEM_POISONING=1 -DKRN_MINIMAL_TIME_SLICE=1
        -DKRN_RESCHED_ENABLE_PREEMPT=1 -DTIMER_HZ=500
    steps:
      - uses: actions/checkout@v4
      - run: printenv
      - run: ./scripts/build_generators/gcc_gcov
      - run: make -j
      - run: make -j gtests
      - run: ./build/gtests
      - run: ./build/st/run_all_tests -c
      - run: ./build/st/run_interactive_test -a

  build-matrix:
    runs-on: ubuntu-24.04
    container: vvaltchev/tilck-i386:v7
    strategy:
      fail-fast: false
      matrix:
        gen: [gcc, gcc_rel, gcc_fast_rel, gcc_nocow,
              gcc_no_nested_irq_tracking, minimal, gcc_small_offt]
    env:
      CMAKE_ARGS: >-
        -DKERNEL_UBSAN=1 -DKRN_KMALLOC_FREE_MEM_POISONING=1 -DKERNEL_SAT=1
        -DKRN_HANG_DETECTION=1 -DKRN_MINIMAL_TIME_SLICE=1
        -DKRN_RESCHED_ENABLE_PREEMPT=1 -DTIMER_HZ=500
    steps:
      - uses: actions/checkout@v4
      - run: printenv
      - run: ./scripts/build_generators/${{ matrix.gen }}
      - run: make -j
      - run: make -j gtests
      - run: ./build/gtests
      - run: ./build/st/run_all_tests -c

  coverage:
    runs-on: ubuntu-24.04
    container: vvaltchev/tilck-i386:v7
    env:
      DUMP_COV: 1
      REPORT_COV: 1
      CMAKE_ARGS: >-
        -DMOD_acpi=0 -DEXTRA_VIM=1 -DKRN_FB_CONSOLE_BANNER=0
        -DKRN_FB_CONSOLE_CURSOR_BLINK=0 -DBOOT_INTERACTIVE=0
        -DPREFERRED_GFX_MODE_W=640 -DPREFERRED_GFX_MODE_H=480
    steps:
      - uses: actions/checkout@v4
      - run: printenv
      - run: ./scripts/build_generators/gcc_gcov
      - run: make -j
      - run: make -j gtests
      - run: ./build/gtests
      - run: ./build/tests/unit/scripts/generate_test_coverage_report
      - run: ./build/st/run_all_tests -c
      - run: ./build/st/run_interactive_test -a
      - run: ./build/scripts/generate_kernel_coverage_report   # no --codecov
      - name: Upload coverage to Codecov
        uses: codecov/codecov-action@v5
        with:
          token: ${{ secrets.CODECOV_TOKEN }}
          files: ./build/coverage.info
          disable_search: true
          fail_ci_if_error: false
```

---

## 7. Worked example — consolidated `ci-toolchain.yml` (NOT adopted — see §4)

Collapses the 6 `tc-*.yml` files into one matrix. Package lists differ per
leg, so they ride along as a matrix variable and are installed in a loop.

```yaml
name: Toolchain builds

on:
  push:
    branches-ignore: [readme*, temp-*, exp-*]
    paths:
      - '.github/workflows/ci-toolchain.yml'
      - 'scripts/build_toolchain'
      - 'scripts/pkgmgr/**'
      - '!README.md'
      - '!**/*.md'

env:
  RUNNING_IN_CI: 1
  GTEST_SHUFFLE: 0

jobs:
  toolchain:
    runs-on: ubuntu-22.04
    container: ${{ matrix.image }}
    strategy:
      fail-fast: false
      matrix:
        include:
          - { name: debian-i386,   image: 'vvaltchev/debian:v5',    arch: i386,
              pkgs: 'gtest lcov libmusl ncurses micropython vim tfblib lua treecmd tcc freedoom fbdoom' }
          - { name: debian-riscv64, image: 'vvaltchev/debian:v5',   arch: riscv64,
              pkgs: 'gtest lcov ncurses treecmd tcc' }
          - { name: fedora-i386,   image: 'vvaltchev/fedora:v4',    arch: i386,
              pkgs: 'gtest lcov libmusl ncurses micropython vim tfblib lua treecmd tcc freedoom fbdoom' }
          - { name: fedora-riscv64, image: 'vvaltchev/fedora:v4',   arch: riscv64,
              pkgs: 'gtest lcov ncurses treecmd tcc' }
          - { name: arch-i386,     image: 'vvaltchev/archlinux:v7', arch: i386,
              pkgs: 'gtest lcov libmusl ncurses vim tfblib lua treecmd tcc freedoom fbdoom' }  # no micropython
          - { name: arch-riscv64,  image: 'vvaltchev/archlinux:v7', arch: riscv64,
              pkgs: 'gtest lcov ncurses treecmd tcc' }
    env:
      ARCH: ${{ matrix.arch }}
    steps:
      - uses: actions/checkout@v4
      - run: printenv
      - name: Run pkgmgr unit tests
        run: sudo -E ./scripts/build_toolchain -t
      - name: Install pkgs
        run: sudo -E ./scripts/build_toolchain
      - name: Build toolchain packages
        run: |
          for p in ${{ matrix.pkgs }}; do
            sudo -E ./scripts/build_toolchain -s "$p"
          done
      - name: Build the kernel
        run: make -j
      - name: Build the unit tests
        run: make -j gtests
      - name: Run the unit tests
        run: ./build/gtests
      - name: Run the system tests
        run: ./tests/runners/ci_run_all_tests_wrapper -c
```

Tradeoff: folding the per-package `-s` steps into one loop loses the
per-package `displayName` in the UI. If that visibility matters, keep them as
individual `run:` steps per leg — but then the package differences can't be a
single matrix var, pushing back toward one file per combo. Recommendation:
accept the loop; the pkgmgr already prints clear per-package progress.

`sudo -E` works as-is: the images already ship `sudo` (Azure uses it today),
and job-level `env:` values are exported so `-E` preserves them.

---

## 8. Migration checklist

1. **Branch** `ci/github-actions`.
2. Add `.github/workflows/ci-i386.yml`, `ci-riscv64.yml`, `ci-x86_64.yml`,
   and `ci-tc-<distro>-<arch>.yml` × 6 (§4). Resolve **O1** (riscv64 matrix).
3. Add the **`CODECOV_TOKEN`** GitHub Actions secret (repo → Settings → Secrets
   and variables → Actions) — the same upload token used today in Azure.
4. Optional: add `on.pull_request` (**O2**) and `concurrency:` blocks.
5. Run the workflows on the branch (push triggers path-filter; may need a
   throwaway edit under a watched path, or a temporary `workflow_dispatch:` to
   trigger manually). Verify:
   - all container jobs pull and start (no Docker Hub rate-limit / node-in-
     container issues),
   - `actions/checkout` + `get_commit_hash`'s `git rev-parse` succeed inside
     the container (watch for git "dubious ownership"; if it appears, add a
     `git config --global --add safe.directory "$GITHUB_WORKSPACE"` step —
     see risk **R1**),
   - the matrix legs run in parallel and independently,
   - coverage jobs produce `coverage.info` and the CodeCov upload appears on
     codecov.io for the commit (kernel `build/coverage.info` on both arches;
     `CODECOV_TOKEN` must be set as a GH secret).
6. Update **README badges** (§9).
7. Once GHA is green on `master`, **disable** the Azure DevOps pipeline
   definitions (or delete `other/ci/` in a follow-up commit). Keep the files
   for one release cycle as a rollback path, then remove.
8. Decide the fate of `.circleci/config.yml` (**O5**) — it overlaps
   heavily with the new GHA coverage/build jobs and may be retired.

---

## 9. README badge updates

Replace the Azure `dev.azure.com/.../_apis/build/status/...` badges (table at
`README.md:6-11`) with GHA workflow-status badges:

```
[![Tilck i386](https://github.com/vvaltchev/tilck/actions/workflows/ci-i386.yml/badge.svg)](https://github.com/vvaltchev/tilck/actions/workflows/ci-i386.yml)
```

…one per workflow. The **CodeCov badge (`README.md:13`) stays unchanged** —
it already points at `codecov.io/gh/vvaltchev/tilck`. The 6 per-case toolchain
badges are preserved (one workflow file each), so the README badge table keeps
its current shape — each cell just swaps its Azure badge URL for the GHA one.

---

## 10. Risks & open questions

**Risks (all low, all with a known mitigation):**

- **R1 — git "dubious ownership" in container jobs.** Running as root over a
  workspace owned by another UID can make `git rev-parse` fail.
  `actions/checkout@v4` adds the workspace to git's global `safe.directory`,
  which usually covers the later build-time git call; if not, add one
  `git config --global --add safe.directory "$GITHUB_WORKSPACE"` step. Verify
  in step 5.
- **R2 — node-in-container for JS actions.** `actions/checkout` and
  `codecov-action` run Node inside the container; needs a glibc ≥ 2.28. All
  six images are modern Debian/Fedora/Arch dev images, so this is fine, but
  it's the classic container-job failure mode — watch the first run.
- **R3 — Docker Hub anonymous pull limits** (§3.6). Mitigation: mirror to
  GHCR if it bites.
- **R4 — CodeCov file scope.** The upload sends only `build/coverage.info`
  (kernel); the unit report is excluded by Codecov's test-path filtering, as
  today. Covered in §5.1; verify the codecov.io report appears for the commit.

**Open questions to confirm before implementing:**

- **O1** — `riscv64.yml` `gcc_13_3`: drop the no-op matrix to a single job,
  or fix the step to use `$(GEN)`? (Pre-existing bug.)
- **O2** — Add `pull_request` triggers (new behavior), or keep push-only to
  match Azure exactly?
- **O3** — *(resolved)* keep 9 files (one per Azure pipeline); a matrix was
  tried but reverted because GHA badges are per-file and each toolchain case
  needs its own status badge.
- **O4** — *(resolved)* the in-repo `--codecov` script is modernized to the
  Codecov CLI (see §5.4), so the local/manual path keeps working.
- **O5** — Retire `.circleci/config.yml` after the move, or keep CircleCI as
  a secondary provider?
```
