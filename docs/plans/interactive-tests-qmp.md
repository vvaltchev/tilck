# Plan: drive the interactive tests over QMP instead of the HMP on stdio

## Goal

`run_interactive_test` talks to QEMU through the human monitor (HMP)
on QEMU's stdin/stdout. It types `sendkey`/`screendump`/`quit` lines,
then reads back what a human would see: the readline echo of its own
command, wrapped in `ESC[K`/`ESC[D` cursor sequences, mixed with
whatever else QEMU writes to stdout *and* stderr, since both are
merged into the same pipe. Every command is followed by a fixed sleep
and a polling read, because the text stream has no notion of "the
reply is complete".

Replace that with QMP, QEMU's JSON machine protocol: one request, one
JSON reply, no scraping, no sleeps to guess completion. Use only
commands old enough to work on every QEMU we build (6.2.0 through
11.1.0) and well beyond: every command in this plan has existed since
QEMU 1.3 or earlier.

Test bodies (`tests/interactive/*.py`), the expected screens under
`tests/interactive/expected/`, `pnm2text` and the stable-screenshot
algorithm do not change. The output of the runner is the same PPM
files, read the same way.

## What the runner uses the monitor for today

All line numbers in `tests/runners/run_interactive_test`.

- **Launch** (`run()`, 725-751): `-monitor stdio`, `-serial pty`,
  QEMU's stdin/stdout as PIPEs, `stderr=subprocess.STDOUT`. Then
  `fh_set_blocking_mode(stdout, False)` (755) so the reads below can
  poll.
- **Reading** (`recv_from_qemu_monitor`, 83-119): non-blocking read
  loop: sleep 50 ms while text keeps coming, up to N retries of
  100 ms when it does not. `qemu_process_tty_text` (147) strips
  `ESC[K` and keeps what follows the last `ESC[D`, the comment above
  it (121-145) explains the readline echo it is undoing.
  `echo_qemu_monitor` (152) prints the result to the runner's output.
- **Writing** (`send_to_qemu_monitor`, 158-176): write a line, sleep
  `delay` (default 200 ms), echo. `BrokenPipeError` sets
  `g_qemu_died` (172), which `main()` (1089) uses to retry the whole
  run up to twice.
- **Keys** (`send_single_key_to_vm`, 178): `sendkey <name>` with a
  50 ms delay per key, so every key costs the delay plus at least two
  read attempts: ~100 ms and more. A 38-key line such as
  `vim /usr/lib/vim/samples/numbers.txt{ret}` takes about 4 s to
  type. `send_string_to_vm` (193) parses the `{name}` syntax.
- **Screenshots** (`vm_take_screenshot`, 265): `screendump sNNN.ppm`,
  relative to QEMU's cwd (the runner `chdir`s into `TMP_DIR` before
  starting QEMU, 808), followed by the 200 ms default delay.
- **Serial port** (`run()`, 759-763): the first read of the monitor is
  the greeting; a regex over it finds
  `char device redirected to /dev/pts/N`, which the coverage dump
  (`dump_coverage_data`, 668) opens with pySerial.
- **Cosmetics**: `send_to_qemu_monitor("\n")` at 319, 439, 460, 510,
  937 exists only to make the HMP prompt reappear after a screenshot
  was printed. `send_to_qemu_monitor("")` at 796 likewise.
- **Quit** (797): `quit`, then `wait_for_qemu_to_exit`.
- **Key names** (`tests/runners/lib/qemu.py`): `QEMU_SPECIAL_KEYS`
  maps punctuation to HMP names (`"!"` -> `shift-1`), `KEYS_MAP`
  covers ASCII 27-127 with a nested conditional expression. Those
  names are QEMU's `QKeyCode` names, which QMP uses too.

## QMP facts, verified

From the QAPI schema (`qapi/ui.json`, `qapi/char.json`) and
`ui/input.c` of the 6.2.0 and 11.1.0 tarballs in `toolchain5/cache/`,
then exercised on every QEMU installed here (stack-built 6.2.0, 7.2.0,
8.2.0, 9.2.0, 10.2.0, 11.1.0, and the system's 11.1.1), all with one
identical session:

| Command            | Since | What we use it for                        |
|--------------------|-------|-------------------------------------------|
| greeting           | 0.13  | `{"QMP": {"version": ..}}` on connect     |
| `qmp_capabilities` | 0.13  | mandatory handshake, no arguments         |
| `query-chardev`    | 0.14  | `serial0` -> `filename: "pty:/dev/pts/N"` |
| `send-key`         | 1.3   | `keys: [{type: qcode, data: name}, ..]`   |
|                    |       | and an optional `hold-time` (ms)          |
| `screendump`       | 0.14  | `filename`: writes a PPM (`P6`); replies  |
|                    |       | after the file is written                 |
| `quit`             | 0.14  | replies `{"return": {}}`, then exits 0    |

Not used, on purpose: `input-send-event` (2.2, more general, more
verbose), `screendump`'s `format`/`device`/`head` (2.12 / 7.1),
`query-status`, out-of-band execution. Nothing here needs them.

Behaviour of `send-key` (same code in 6.2 and 11.1, and the HMP
`sendkey` the runner uses today calls the same `qmp_send_key`):

- Each key in `keys` is pressed in order, held, then released in
  reverse order. `[alt, f2]` is the chord `alt-f2`.
- Press/release events go through one input queue timed on the
  *virtual* clock: a press is delivered at once when the queue is
  idle, everything else is queued behind a `hold-time` delay. A
  single key therefore occupies the guest for `2 × hold-time`, and a
  burst of N commands drains in `N × 2 × hold-time` of guest time.
- The queue holds 1024 entries and **silently drops** past that; one
  key costs up to 4 entries (press, delay, release, delay), so a
  burst must stay under 256 keys.
- The default `hold-time` is 10 ms in every version we have (the
  QAPI doc says 100 ms; the code says `kbd_default_delay_ms = 10`).
  We pass it explicitly, so the value does not depend on the version.
- An unknown key name is a `GenericError` reply, not a crash.

Delivery to Tilck, checked end to end with the prototype in this
plan's session: a 63-key line typed as one burst of `send-key`
commands (issued in ~20 ms) is echoed intact by the shell, under KVM
and under TCG, at hold times of 10, 20 and 50 ms.

Transport: `qemu_printf` sends `char device redirected to ...` to
QEMU's **stdout**, so `-qmp stdio` would interleave that banner (and
anything else QEMU prints) with the JSON stream. A socket keeps the
protocol stream clean, and leaves stdout/stderr free to be logged.

## Design

### Transport: the runner listens, QEMU connects

```
runner: sock = socket(AF_UNIX); bind(<tmpdir>/qmp.sock); listen(1)
qemu:   ... -qmp unix:<tmpdir>/qmp.sock ...
runner: conn = sock.accept(); read greeting; qmp_capabilities
```

QEMU as the *client* is the original QMP command line
(`-qmp unix:path`, no options): there is no `server`/`nowait` short
form to worry about (deprecated since 6.0, still warned about in
11.1), no `server=on,wait=off` long form to depend on, and no
"poll until QEMU created the socket" loop: the runner is listening
before QEMU is spawned, `accept()` gets the connection whenever QEMU
gets to it. It works the same on Linux, macOS and FreeBSD.

The socket lives in `tempfile.mkdtemp(prefix="tilck-qmp-")`, removed
when the client is closed. Not in `TMP_DIR` (`<build>/tmp`): a Unix
socket path is limited to 104-108 bytes and a build directory can be
anywhere; a temp dir never is.

QEMU's own stdout/stderr no longer carry the protocol, so they go to a
file next to the two logs the runner already keeps
(`QEMU_DEBUG_FILE`, `QEMU_DEBUG_FILE2`): `TMP_DIR/qemu_stdio.log`,
printed under `VERBOSE` and when QEMU dies unexpectedly. Stdin is
`DEVNULL`. `-monitor stdio` is dropped; nothing else on the command
line changes (`-serial pty`, `-display none`, the debugcon and trace
logs, `-enable-kvm -cpu host` when detected).

### The client: `tests/runners/lib/qmp.py`

A small, dependency-free module, in the style of `lib/detect_kvm.py`.
QEMU's own `python/qemu/qmp` package is not used: it is a separate
PyPI dependency with its own asyncio machinery, for four commands.

```python
class QmpClient:
   def __init__(self, timeout):       # mkdtemp, bind, listen
   qemu_arg -> "unix:<path>"          # what to put after -qmp
   def accept(self):                  # accept, greeting, qmp_capabilities;
                                      # returns the greeting's version dict
   def cmd(self, name, **args):       # -> the "return" value
   def close(self):                   # close sockets, rmtree the dir
```

Framing: replies and events are JSON objects on a byte stream. Use
`json.JSONDecoder().raw_decode` over an accumulating buffer, not
line splitting; QEMU happens to terminate each object with `\r\n`
but the protocol does not promise it.

`cmd()` skips `{"event": ..}` objects while waiting for the reply
(logging them under `VERBOSE`; a `RESET` while a test runs is worth
seeing), returns `m["return"]`, and raises:

- `QmpCommandError(class, desc)` on an `{"error": ..}` reply;
- `QemuGone` on EOF, `ECONNRESET`, `EPIPE`: the process is dead;
- `QmpTimeout` when a reply does not arrive within `timeout` (a
  generous constant, 60 s; the outer `join_worker_thread` remains the
  real watchdog, and its SIGINT to QEMU turns into `QemuGone` here).

`accept()` uses the same timeout: a QEMU that never connects (bad
command line, missing binary) is reported as such rather than hanging.

### Key names: `tests/runners/lib/qemu.py` emits key lists

`KEYS_MAP[ch]` becomes a list of `QKeyCode` names: `"a"` ->
`["a"]`, `"A"` -> `["shift", "a"]`, `"!"` -> `["shift", "1"]`. The
`{name}` syntax in test strings is split on `-`: `{alt-f2}` ->
`["alt", "f2"]`, `{ret}` -> `["ret"]`. That is unambiguous because
`QKeyCode` names never contain `-` (`bracket_left`, `grave_accent`).
The nested conditional expression that builds `KEYS_MAP` today
becomes a plain function; the two tables keep their content.

A `keys_for_string(s)` in the same module does the `{..}` parsing that
`send_string_to_vm` does inline today, and returns `[[name, ..], ..]`.
It is pure and has no QEMU in it, so it can be checked by hand.

### The runner's primitives

```python
KEY_HOLD_TIME_MS = 20       # explicit; QEMU's own default is 10
KEY_BURST_MAX    = 128      # queue holds 1024 entries, 4 per key

def vm_send_keys(key_lists):
   for chunk in chunks(key_lists, KEY_BURST_MAX):
      for keys in chunk:
         g_qmp.cmd("send-key",
                   keys = [{"type": "qcode", "data": k} for k in keys],
                   **{"hold-time": KEY_HOLD_TIME_MS})
      time.sleep(len(chunk) * 2 * KEY_HOLD_TIME_MS / 1000.0)

def send_string_to_vm(s):
   vm_send_keys(qemu.keys_for_string(s))

def vm_take_screenshot():
   path = os.path.join(TMP_DIR, "s{:03}.ppm".format(g_next_screenshot))
   g_qmp.cmd("screendump", filename = path)   # returns after the write
   ...

def qemu_serial_pty():
   for c in g_qmp.cmd("query-chardev"):
      if c["label"] == "serial0" and c["filename"].startswith("pty:"):
         return c["filename"][len("pty:"):]
   raise ...
```

Pacing model. Today the guest's typing rhythm is a side effect of
host sleeps and poll loops. With QMP the rhythm is QEMU's input queue
on the virtual clock, and the runner knows exactly when it drains:
`N × 2 × hold-time` after the last command. It waits that long, then
the existing `vm_take_stable_screenshot` does what it always did
(two identical shots 250 ms apart, blink tolerance, 3 s cap). The
stable-screenshot loop is not asked to detect "keys still arriving";
the drain wait guarantees they have.

Hold time 20 ms: twice QEMU's default, well inside what a PS/2
driver sees from a real keyboard, and ~2.5 s for the longest burst in
the suite (63 keys) instead of the 6 s and more the HMP path needs.
It is one named constant; raising it is a one-line change if a slower
guest ever needs it.

`KEY_BURST_MAX` is a guard against the silent drop, not a feature: no
test string comes near it.

The screenshot path is absolute, so it no longer depends on QEMU's
cwd; the runner still `chdir`s into `TMP_DIR` for `filecmp` and
`delete_old_screenshots`.

### `run()`, rewired

```
client = QmpClient(QMP_TIMEOUT)
args = [QEMU_BIN, ..., '-qmp', client.qemu_arg, ...]     # no -monitor
p = Popen(args, stdin=DEVNULL, stdout=<qemu_stdio.log>, stderr=STDOUT)
version = client.accept()                                # greeting
pts_file = qemu_serial_pty()
run_main_body(); coverage dump as today
client.cmd("quit"); wait_for_qemu_to_exit(); client.close()
```

Deleted: `recv_from_qemu_monitor`, `qemu_process_tty_text` and its
40-line comment, `echo_qemu_monitor`, `send_to_qemu_monitor`,
`send_single_key_to_vm`'s HMP string building, the greeting regex,
the `fh_set_blocking_mode` call (the helper stays in `lib/utils.py`
for `single_test_run`), the five cosmetic `"\n"` sends and the empty
send before `quit`.

Error mapping keeps today's outcomes:

- `QemuGone` replaces `BrokenPipeError` in `run_main_body` (580) and
  `send_to_qemu_monitor` (169): it sets `g_qemu_died`, and `main()`'s
  retry-twice logic (1089) is untouched.
- `QmpCommandError` propagates like any other exception:
  `Fail.other`, with the class and description in the message. A
  `screendump` that QEMU cannot write is now an explicit error
  instead of a missing file one step later.
- `QmpTimeout` -> `Fail.timeout`, next to `StableScreenshotFailure`.

The `qemu_kvm_version` argument, `detect_kvm`, `-enable-kvm -cpu
host`, `join_worker_thread`'s SIGINT, `KeyboardInterrupt` handling,
`GEN_TEST_DATA` mode and `dump_coverage_data` are unchanged.

## What this does not touch

- `tests/interactive/*.py`: they call `send_string_to_vm`,
  `send_to_vm_and_find_text`, `do_interactive_actions`,
  `vm_take_stable_screenshot`, `img_convert`, `just_run_vim_and_exit`
  and nothing lower. Same names, same semantics.
- `tests/interactive/expected/*`: the screenshots are the same PPMs of
  the same screens; `GEN_TEST_DATA=1` must reproduce them byte for
  byte (that is the acceptance test below).
- `single_test_run` and `run_all_tests`: the former never had a
  monitor (serial console over `-nographic`), the latter only forwards
  the KVM answer. `lib/detect_kvm.py` is unchanged.
- `-d cpu_reset,guest_errors,trace:*monitor*`: the `monitor_qmp_*`
  trace points cover QMP; the trace log keeps its value.

## Implementation sequence

Each step compiles and passes the interactive suite on its own.

1. **`lib/qmp.py`**: the client, alone. Exercised by a throwaway
   script against every stack QEMU (the prototype from this session,
   which is the same session the runner will use).
2. **`lib/qemu.py`**: `KEYS_MAP` to lists, `keys_for_string`. At this
   step `send_single_key_to_vm` joins the list with `-` back into an
   HMP name, so the runner still works over HMP. A 5-line sanity check
   that every ASCII 32-126 maps to one or two known `QKeyCode` names.
3. **`run_interactive_test`**: switch the transport. `run()` as
   above; `vm_send_keys`, `send_string_to_vm`, `vm_take_screenshot`,
   `qemu_serial_pty` on top of the client; delete the HMP code and
   the cosmetic sends; map the exceptions. The constants
   (`KEY_HOLD_TIME_MS`, `KEY_BURST_MAX`, `QMP_TIMEOUT`) next to
   `STABLE_SCREENSHOT_*` in `lib/exceptions.py`, or wherever those
   end up.
4. **`docs/testing.md`** (line 245): "QEMU's monitor has an interface
   for sending keystrokes" still holds; one clause naming QMP and
   `send-key`, so the doc matches what the reader will find in the
   runner.

## Verification

- `./build-intr/st/run_all_tests -T interactive`: 6/6, under KVM.
- `RUNNING_IN_CI=1 ./build-intr/st/run_all_tests -T interactive`:
  the same under TCG (the CI env flag makes `detect_kvm` assume no
  KVM, which is what CI runs with).
- Every QEMU we build: `PATH=<stack bin>:$PATH` for each of 6.2.0,
  7.2.0, 8.2.0, 9.2.0, 10.2.0, 11.1.0 (`./scripts/build_toolchain -q
  --print-layout | grep QEMU_` lists the bin dirs), plus the system
  one. Roughly 2 minutes each.
- `GEN_TEST_DATA=1 ./build-intr/st/run_interactive_test -a` writes a
  fresh `<test>-<n>.gz` per screen into `TMP_DIR`. Compare each with
  its twin in `tests/interactive/expected/` **decompressed**
  (`gzip -dc`; a gzip header carries an mtime, so the files differ
  even when the text does not): no difference. This is the proof that
  the guest saw the same keys in the same order and the screens are
  the same. `fbtest` is not regenerated by that mode, it compares a
  PNG; the normal run covers it.
- `VERBOSE=1` once, to see the events and the stdio log come out.
- A deliberately bad key name in a scratch test body, to see the
  `GenericError` reported as a test failure with its description.
- `Ctrl-C` during a run, to see the KeyboardInterrupt path still stops
  QEMU and the runner.

## Compatibility envelope

By command set, any QEMU with `send-key` `qcode` keys: 1.3 (2012)
and later. Tested: 6.2.0 through 11.1.1. Hosts: Linux, macOS and
FreeBSD all have `AF_UNIX`, `tempfile` and `json`; the runner drops
its only host-specific trick (the non-blocking `fcntl` on QEMU's
stdout).

## Open choices, with the recommended answer

- **Hold time**: 20 ms (above). The one number that trades speed for
  margin; if a CI box under TCG ever proves too slow, the constant is
  the place.
- **Where QEMU's stdio goes**: a file in `TMP_DIR`, dumped under
  `VERBOSE` and on unexpected death. Not a reader thread: nothing in
  the runner needs those lines live.
- **Events**: skipped and logged. Acting on `RESET`/`SHUTDOWN` (a
  panic that reboots, a guest that powers off) would be a separate
  change; the hooks are there.
