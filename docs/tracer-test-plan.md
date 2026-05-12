# `tracer --test` plan (to be implemented after the split)

## Context

After the tracer is split out of `dp` into its own userspace app
(`userapps/tracer/`), we add a `--test` mode that exercises the
tracer pipeline end-to-end. The fcntl panic we hit earlier was a
dispatcher-invariant bug that pure-rendering tests would have
missed — the test plan is shaped around catching that class of
bug first.

## Three tiers, ranked by leverage

### Tier 1 — Live-syscall integration (highest leverage)

In `tracer --test`, mark the process itself as traced, run a
curated sequence of real syscalls, read the resulting events back
from `/syst/tracing/events`, and assert:
- No kernel panic.
- Each event has the expected `sys_n`, `tid`, `retval`.
- The rendered string contains expected substrings.

Catches the kernel save dispatcher + slot allocator (the surface
that produced the fcntl panic). About 200 LOC of test logic, one
test case per ptype family.

### Tier 2 — Event injection

Two new TILCK_CMDs in `modules/tracing/tracing_cmd.c`:

- `TILCK_CMD_DP_TRACE_SET_TEST_MODE(on)` — sets a kernel flag.
- `TILCK_CMD_DP_TRACE_INJECT_EVENT(struct trace_event __user *)`
  — copies the user-supplied event verbatim into the ring buffer,
  but **only** when test mode is on (returns `-EPERM` otherwise so
  production builds can't be flooded). Skips all save logic — the
  caller is responsible for filling `saved_params` correctly.

In `tracer --test`, build synthetic events covering every ptype +
every `enum trace_event_type`, inject each, read it back, render,
compare. About 400 LOC of test logic + 60 LOC kernel for the new
TILCK_CMDs.

Single-process: inject synchronously, then read back. No fork
needed for the first pass. (A fork-mode variant can be added
later if we want to test concurrent multi-task event delivery.)

### Tier 3 — Pure rendering gtest (SKIP for now)

Recompiling dump callbacks for host-side gtest is straightforward
(no kernel deps after the userspace move), but the value is small
— Tier 1 + Tier 2 cover every per-ptype rendering case with
realistic data, less code, and no host/target divergence risk.

The ONE case where pure unit tests would pay off is the printk
multi-line continuation logic (`dump_trace_printk_event` in
`tr_render.c`). Defer until we hit a printk-render bug we can't
easily reproduce via injection.

## Concrete `tracer --test` shape

```
$ tracer --test
[Live] Phase A — real syscalls
  [PASS] open + close: events have correct flags
  [PASS] fcntl F_GETFL: cmd renders, arg = (unused), no panic
  [PASS] fcntl F_SETFL: arg renders as O_NONBLOCK
  [PASS] waitpid: wstatus decodes WIFEXITED
  [PASS] poll: pollfd ptr captured
  [PASS] mmap: prot/flags render symbolically
  [PASS] ioctl TIOCGWINSZ: argp renders as struct winsize
  ... ~15 cases ...

[Inject] Phase B — synthetic events
  [PASS] te_sys_enter, no params
  [PASS] te_sys_exit, errno return
  [PASS] te_printk single line
  [PASS] te_printk multi-event continuation
  [PASS] te_printk with "{...}" truncation
  [PASS] te_signal_delivered SIGTERM
  [PASS] te_killed SIGKILL
  [PASS] ioctl_argp termios layout
  [PASS] ioctl_argp winsize layout
  [PASS] mmap_prot bitmask
  [PASS] mmap_flags bitmask
  [PASS] wstatus all decode paths
  [PASS] wstatus signaled
  [PASS] unknown ptype id falls back gracefully
  [PASS] "<fault>" marker handled
  ... ~20 cases ...

35/35 PASS
```

Plus:

- **Pre-injection wire sanity assertion**: `tracer --test` reads
  `/syst/tracing/metadata`, validates magic + version, prints
  per-ptype id + name + slot_size in verbose mode. Regression
  check for wire-format drift.

- **A `--stress` flag**: inject 10k events in a tight loop, verify
  all come back with correct tid/sys_n. Tests ring-buffer overrun.

## File touch list

Kernel (new in `modules/tracing/tracing_cmd.c`):
- TILCK_CMD enum bumps + handler funcs for SET_TEST_MODE and
  INJECT_EVENT.
- A `__tracing_test_mode` static bool.

Userspace (new in `userapps/tracer/`):
- `test_main.c` — the `--test` dispatcher + test driver.
- `test_inject.c` — Tier 2 injection cases.
- `test_live.c` — Tier 1 live-syscall cases.

CMake: no new target — `tracer` already exists post-split. Just
add the test files to its source list.

## Wiring into CI

Could be added as a stage in `./build/st/run_all_tests`. The
binary is small (a `tracer --test` exec) and self-contained, so
the test runner just needs to spawn it and check the exit code.
