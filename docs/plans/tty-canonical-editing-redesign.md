# TTY canonical line-editing redesign — plan & post-mortem

Status: **RESOLVED (2026-06-05)** — implemented on `exp-work`, verified live and
across configs. See the RESOLUTION section directly below; the rest of the
document is kept as the post-mortem and reference.

This document is the spec / post-mortem. It is intentionally exhaustive.

---

## RESOLUTION (2026-06-05)

Step 0 (verify-first) found the real root cause, and it was **not** a
fundamental architecture problem: the ~250 ms ring drain was a **regression from
my own commit `6a10c2cbb`** ("Fix lost-wakeup race in tty_read_int()"). That
commit swapped `KCOND_WAIT_FOREVER` for a ~250 ms timeout but kept the 2019 wait
predicate `tty_inbuf_is_empty()`. Under `FOREVER` a canonical reader only ever
woke on a *delimiter* signal (`input_cond` is signalled only on a line delimiter
in canonical mode), so regular chars accumulated in `input_ringbuf` until ENTER.
With the periodic timeout the reader wakes every ~250 ms and the loose
"ring non-empty" predicate drains the half-typed line into `read_buf` early.

Q1 confirmed it live: slow backspace edited only the screen (`cat` got `abc` for
`ab`+pause+⌫+`c`); fast backspace edited the buffer (`cat` got `gi`).

**Fix** (the `[tty] tty_read_int: wait for data-ready` commit): wait on
`tty_read_ready_int()` (the real read-ready
condition == exactly what `input_cond` signals: a completed line in canonical,
`VMIN` in raw) instead of `tty_inbuf_is_empty()`, in both the block loop and the
`O_NONBLOCK` `EAGAIN` gate. `input_ringbuf` again holds the whole canonical line
until ENTER, so VERASE/backspace edits it reliably.

With the foundation correct, the first attempt's commits are **correct as
written** (the `if (ret)` erase bound is reliable now, so `col_offset` removal
works). They were re-applied on top of the fix and **verified LIVE** — the step
the first attempt skipped:
- `^A` control char + ⌫ → both caret cells erased; `cat` got the line without it.
- tab + ⌫ → tab byte dropped, cursor jumps the full tab width.
- extra ⌫ at the input start → no-op, prompt/previous line untouched (no col_offset).
- 90-char wrapped line → erased across the wrap.

plus 102 console gtests, i386 release+UBSAN, riscv64+UBSAN, style clean, and the
interactive suite (tracing/vim1/vim2/ls, which require `BOOT_INTERACTIVE=0` +
`MOD_fb=1`).

So §6.1 (the discipline column tracker) and §6.3 Option B turned out
**unnecessary** — the single predicate fix restored the reliable ring and
everything else fell out. §11's lesson stands: the gtests are blind to this
(they drive the discipline synchronously, with no `read()` draining), so live
testing was essential.

The first attempt (reverted, not retained as a branch) is summarised in §2; its
corrected versions are this series' current `[tty]` / `[console]` commits.

---

## 1. Goal

Make Tilck's canonical-mode line editing correct and Linux-faithful, and remove
the `col_offset` hack from the **video term** — moving any needed input-position
tracking into the **tty line discipline** (this was the user's explicit
preference: *"if col_offset needs to stay, but moved into the tty file, that's
great"*).

Concretely, the end state should deliver, with no per-cell screen metadata
waste and no regressions:

1. **Control-char caret erase.** A control byte echoes in caret notation
   (`^X`, two cells) under `ECHOCTL`. Backspacing it must clear **both** cells,
   not leave a stray `^`. An UP-arrow (`ESC [ A`, echoed `^[[A`) must erase
   cleanly with three backspaces.
2. **Exact tab erase.** Backspacing a tab must return the cursor to exactly
   where the tab began (better than Linux, which re-derives and can overshoot).
3. **Tab marks consistent across scroll/clear** (no stale marks on reused rows).
4. **Backspace wraps to the previous row** for a line that wrapped.
5. **`col_offset` removed from the video term** (the user's main ask), replaced
   by a reliable mechanism that does **not** depend on `input_ringbuf`.

NON-negotiable constraints (from CLAUDE.md): boot time sacred, hot-path latency
sacred, BSD-2-Clause originality (reimplement n_tty ideas from scratch, never
copy Linux code/identifiers), Tilck coding style.

---

## 2. The failed attempt (what was tried)

The first attempt was five commits (reverted, then re-applied correctly after
the fix — see RESOLUTION). What each did:

- **C1** — `[tty] canon mode: erase caret-echoed control chars fully`.
  Rewrote `tty_inbuf_drop_last_written_elem()` to (a) *unwrite first* to recover
  the dropped byte via `ringbuf_unwrite_elem(&input_ringbuf, &c)`, then (b) echo
  VERASE `tty_echo_width(c)` times **only `if (ret)`** (only if a byte was
  actually unwritten). Added helpers `tty_char_echoed_as_caret()` /
  `tty_echo_width()`. **This is the commit that broke backspace** — see §4.
- **C2** — `[console] store tab width in tabs_buf; drop the col_offset
  hack`. Changed `tabs_buf` from `bool*` to `u8*`, stored the tab's *width* at
  its last cell, made the term backspace jump back by that width, and **deleted
  `col_offset` entirely** (field, `term_action_set_col_offset`, the
  `a_set_col_offset` opcode, the `set_col_offset` term-interface slot + serial
  no-op, and the discipline's `set_col_offset(-1)` call in `tty_read_int`).
- **C3** — `[console] keep tabs_buf consistent across scroll and clear`.
  `buf_copy_row()`/`ts_buf_clear_row()` carry the tab row; main scroll shifts
  `tabs_buf` up one row.
- **C4** — `[console] backspace wraps to the previous row`. Term
  backspace, at the left margin, steps to `(r-1, cols-1)`.
- **C5** — `[tests] console: cover canonical line-editing end to end`.

All 304 gtests passed. i386 release+UBSAN and riscv64+UBSAN built clean.
Interactive regression (tracing/vim/ls/dp) passed. **And backspace was still
broken** — because the gtests and those regressions never exercise the real
canonical-erase path (see §8). The user caught it immediately on real hardware
behaviour.

---

## 3. The canonical input architecture (verified facts)

All paths in `kernel/tty/tty_input.c` unless noted. Function names are stable;
line numbers drift — search by name.

### 3.1 Keypress → discipline

- `kb` IRQ → `tty_keypress_handler_int()`. If `ke.print_char == 0` it's a
  non-printable key → `tty_handle_non_printable_key()` (arrows etc. emit an ANSI
  seq). Backspace has `print_char == 0x7f` (non-zero) so it goes the normal way.
- → `tty_send_keyevent()` → CR/NL translation → `tty_handle_special_controls()`
  (signals: VINTR/VEOF/VSUSP… — **not** VERASE) → if `ICANON`,
  `tty_keypress_handle_canon_mode()`.
- `tty_keypress_handle_canon_mode()`:
  ```c
  if (c == c_cc[VERASE])
     tty_inbuf_drop_last_written_elem(t);   // erase path
  else {
     tty_inbuf_write_elem(t, c, block);     // writes input_ringbuf + echoes
     if (line_delim) { end_line_delim_count++; kcond_signal_one(input_cond); }
  }
  ```
  KEY: **VERASE is consumed here. It is never written to `input_ringbuf`.** So
  the read path (below) never sees a VERASE byte.
- `tty_inbuf_write_elem()` writes the byte to `input_ringbuf` **and** echoes it
  via `tty_keypress_echo()`. Regular chars **do not** signal `input_cond`.

### 3.2 Echo → term

- `tty_keypress_echo(t, c)`: `ECHONL`/`ECHO` gates; in `ICANON`: VEOF skipped;
  `ECHOK`+VKILL; **`ECHOE`+(VERASE|VWERASE) → writes the raw VERASE byte (0x7f)
  to the term** and returns; else caret branch
  `(c < ' ' || c == 0x7F) && ECHOCTL && c != '\t','\n',VSTART,VSTOP` → writes
  `"^" , c+0x40`; else writes `c` literally.
- The term's filter dispatches the written byte. `tty_update_default_state_tables()`
  (`modules/console/console_def_state.c.h`) sets, dynamically,
  `def_state_funcs[c_cc[VERASE]] = tty_def_state_verase` (overrides the static
  `def_state_funcs[0x7f] = tty_def_state_ignore`). `tty_def_state_verase` emits
  `term_make_action_del_prev_char()` → `a_del_generic`/`TERM_DEL_PREV_CHAR` →
  `term_action_del()` → `term_internal_write_backspace()` in
  `modules/console/video_term.c`. So the **screen erase is driven by echoing
  the VERASE byte to the term**, not by the unwrite.
- `'\b'` (0x08) is separately `def_state_funcs['\b'] = tty_def_state_backspace`
  → non-destructive `move_cursor_rel(0,-1)`. Different from VERASE.
- The term action queue runs the per-char filter + action and finally
  `move_cursor(t->r, t->c, …)` once at the end of the write action
  (`modules/console/term_actions.c.h`, end of `term_action_write`). A backspace
  that changes `t->r`/`t->c` redisplays the cursor correctly.

### 3.3 Default termios (`kernel/tty/tty_ioctl.c`)

`c_lflag = ISIG|ICANON|ECHO|ECHOE|ECHOK|ECHOCTL|ECHOKE|IEXTEN`.
`c_cc[VERASE] = 0x7f`. **ECHOE is on**, so the VERASE echo takes the
`ECHOE`-write-verase branch (not the caret branch). The Backspace key delivers
0x7f, matching VERASE.

### 3.4 The read side — WHERE THE EDITABLE LINE ACTUALLY LIVES

`tty_read_int()` and `tty_internal_read_single_char_from_kb()`:

- A canonical `read()` blocks in `while (tty_inbuf_is_empty(t)) kcond_wait(input_cond,
  …, KRN_TIMER_HZ/4)`. Regular chars **don't** signal `input_cond`, so the
  reader wakes either on a line-delimiter signal **or on the ~250 ms timeout**
  (`KRN_TIMER_HZ/4`; KRN_TIMER_HZ is 250 → ~250 ms).
- On wake, the drain loop
  `while (!inbuf_empty && read_buf_used < TTY_READ_BS && read_single_char(…)) {}`
  moves each byte **out of `input_ringbuf` into the per-handle cooked buffer
  `eh->read_buf`** (`read_single_char` does `read_buf[read_buf_used++] = c`).
  `read_single_char` only special-cases **line delimiters**; it has no notion of
  VERASE (VERASE never reaches `input_ringbuf`).
- `tty_internal_should_read_return()` keeps the cooked line until a delimiter,
  then `tty_flush_read_buf()` returns it to userspace.

**Consequence (the crux):** `input_ringbuf` is a *transient staging buffer*.
Within ~250 ms of typing, the bytes are moved into `eh->read_buf`. So when a
backspace arrives after any pause, **`input_ringbuf` is empty** and the
characters being edited are sitting in `read_buf` — which the discipline's
`drop_last`/`unwrite` cannot touch.

This was confirmed live with a `DBG DROP empty` print: after typing `abc` into
`cat` and pausing, `ringbuf_unwrite_elem` returned `false` (empty ring).

---

## 4. Root cause of the regression

The original `tty_inbuf_drop_last_written_elem()` was:

```c
tty_keypress_echo(t, c_cc[VERASE]);                 // (A) ALWAYS echo → screen erase
ret = ringbuf_unwrite_elem(&input_ringbuf, NULL);   // (B) best-effort unwrite
return ret;
```

It worked because **(A) is unconditional**: every VERASE echoes the VERASE byte
to the term, which always does `del_prev_char` → erases a screen cell. The
**`col_offset` floor in the term** (`term_internal_write_backspace`:
`if (!t->c || t->c <= col_offset) return;`) is what kept that unconditional
erase from eating the prompt. The unwrite (B) is genuinely best-effort: it only
removes the byte if it's still in `input_ringbuf` (i.e. typed within the last
~250 ms).

The flawed C1 changed (A) to fire **only `if (ret)`** — only when the unwrite
found a byte. Because `input_ringbuf` is normally empty by the time you
backspace (§3.4), `ret == false`, so **no echo, no screen erase** → "backspace
does nothing". C2 then removed `col_offset`, so even restoring an unconditional
echo would over-erase the prompt.

In short: **C1 tied the visible erase to a buffer that is drained out from under
the editor, and C2 removed the only reliable erase bound.** The unit tests
passed because they drive the discipline synchronously via `feed_key()`, where
nothing drains `input_ringbuf`, so `ret` is always true — a false-confidence
blind spot (see §8).

Bonus realisation: C1's premise ("recover the dropped byte to size the
caret/tab erase width") is unsound for the same reason — the byte usually isn't
in `input_ringbuf` anymore. Width-sizing must come from somewhere reliable
(§6).

---

## 5. Open questions — VERIFY FIRST, before writing any code

Do these on the **baseline** (`06d6049b`) so we design against reality:

- **Q1 — Does baseline backspace edit the *buffer* or only the *screen*?**
  In `cat`: type `abXc` as `a`,`b`, *pause >300 ms*, backspace, `c`, ENTER.
  Does `cat` echo back `ac` (buffer edited) or `abc`/`ab?c` (only the screen was
  erased)? Repeat with a fast burst (`ab`+BS+`c` within ~100 ms). This tells us
  whether baseline has a latent *buffer* bug for slow backspace (likely yes,
  given §3.4) and whether the design must also fix `read_buf` editing or only
  preserve the existing (screen-correct, buffer-best-effort) behaviour.
- **Q2 — Confirm the ~250 ms drain window** by timing how long after a keystroke
  `input_ringbuf` still holds it (temporary `printk` of `ringbuf_get_elems`).
- **Q3 — Where should width-correct caret/tab erase live?** Options in §6;
  decide after Q1 tells us if the discipline can see the line at all.
- **Q4 — Does `read_single_char` ever need to handle VERASE** (i.e. should
  VERASE be cooked into `read_buf` editing) for a *correct* buffer? See §6
  Option B.

Capture answers in this file before coding.

---

## 6. The correct design

Two concerns that the flawed attempt conflated:

1. **Screen erase + bound** — must always erase the right number of cells and
   never cross the input start. The reliable source of "where input started" is
   the **terminal cursor column at `read()` start**, not `input_ringbuf`.
2. **Buffer edit** — removing the char from the line that `read()` returns.

### 6.1 Bound: reliable input-position tracking in the discipline

Replace `col_offset` (term-side) with a discipline-side tracker, exactly per the
user's "col_offset moved to the tty":

- Add a term-interface getter `get_curr_col()` (the value `vterm_get_curr_col()`
  already computes; just expose it). Used by the discipline at canonical
  `read()` start to learn the input-start column.
- In `struct tty`, track `canon_start_col` (set at read start) and a running
  `echo_col` advanced as bytes echo. On VERASE, only erase while
  `echo_col > canon_start_col`; decrement `echo_col` by the erased width.
- This bound is **independent of `input_ringbuf`** and survives the drain.

Caveat already discovered: wrapping. `echo_col` is a logical column; for lines
that wrapped, the physical column differs. Decide whether to track physical
(mod `cols`, needs `cols` from the term) or accept logical for the v1 and refine
later. For a single-row prompt+input (the overwhelming common case) logical ==
physical.

### 6.2 Width-correct caret/tab erase

The discipline cannot reliably see the bytes (drained). Two viable homes:

- **Term-side (recommended for caret pairs).** Have the term know a cell is the
  2nd half of a caret pair, the way `tabs_buf` knows tab spans. This is small,
  current-screen metadata (the kind already accepted for tabs). Backspace lands
  on the trailing cell and erases the pair. This avoids the discipline needing
  the byte. Reconsider whether a single `u8 cellmeta` map can encode BOTH "tab
  width" and "caret-pair" (e.g. distinct nonzero codes) to avoid two arrays.
- **Discipline-side via `echo_col`.** If the discipline tracks `echo_col` and
  the per-byte echoed width as it echoes (a small per-line width stack reset at
  read start / each delimiter — NOT `input_ringbuf`), it can emit the right
  number of `\b`-equivalent erases. This keeps the term dumb but adds a small
  discipline-side structure. Weigh vs. the user's "no wasted buffer".

Pick after Q1/Q3. The **tab-width-in-`tabs_buf`** idea from C2 (storing width,
`u8` not `bool`) and the **C3 scroll-consistency** work are correct and
**should be salvaged** regardless — they are term-side and independent of the
discipline bug.

### 6.3 Buffer correctness (if Q1 shows it's broken)

If baseline only erases the screen for slow backspace, the *correct* fix is to
make the editable line live in one place until the delimiter:

- **Option A — keep cooking, edit `read_buf` too.** The hard part: VERASE is
  consumed in the discipline and never cooked, and `read_buf` is per-handle
  (the discipline can't reach it). Would require routing erase intent into the
  read path. Complex; likely not worth it.
- **Option B — don't drain until a delimiter.** Make the canonical reader keep
  bytes in `input_ringbuf` until a line delimiter, so all editing (insert +
  VERASE unwrite) happens in one buffer and is always correct. Risk:
  `input_ringbuf` overflow on a very long unterminated line (size `TTY_INPUT_BS`)
  and re-justifying the ~250 ms drain timeout that exists for missed wakeups.
  This is the cleanest model but touches `tty_read_int` carefully. **Probably
  the right long-term direction**, but verify the timeout rationale
  (`kb_worker_thread` missed-wakeup comment in `tty_read_int`) before changing
  drain behaviour, and re-test riscv64-without-KVM boot (the comment warns the
  slice/timeout interacts with boot timing).
- **Option C — accept baseline behaviour.** Screen-correct, buffer-best-effort
  (works within the drain window). Lowest risk; doesn't fix a latent bug but
  doesn't regress. Acceptable for a first landing if Q1 shows baseline already
  behaves this way and the user is OK with it.

Recommended sequencing: land §6.1 + §6.2 (restores and improves the **visible**
behaviour with a reliable bound) first; treat §6.3 Option B as a separate,
later, well-tested change.

---

## 7. Implementation plan (commit by commit)

Each commit compiles in all configs, passes gtests, **and is verified live**
(§8) before moving on. Small, single-responsibility, fine-grained.

0. **Investigate (no code).** Answer Q1–Q4 (§5). Write findings into §5 of this
   doc. This decides §6.2/§6.3.

1. **Expose `get_curr_col()` on the term interface.** Add the slot + the video
   implementation (reuse `vterm_get_curr_col`) + serial no-op. No behaviour
   change. (Mirror how `set_col_offset` was wired, but read-only.)

2. **Discipline input-column tracker.** Add `canon_start_col` + `echo_col` to
   `struct tty`; set `canon_start_col` at canonical `read()` start (where the
   old `set_col_offset(-1)` was); advance `echo_col` in the echo path. Don't yet
   change erase behaviour. Add a `STATIC`-exposed accessor so gtests can assert
   the tracked column (STATIC + test-header pattern).

3. **Make VERASE erase use the tracker, not `input_ringbuf`.** Echo VERASE while
   `echo_col > canon_start_col` (bounded), decrement `echo_col`. Keep the
   best-effort `input_ringbuf` unwrite for buffer correctness within the window
   (don't gate the echo on it). This is the commit that *restores* working
   backspace **without** `col_offset`.

4. **Width-correct caret erase** (the original C1 goal, done right per §6.2).
   Verify arrows/control chars erase fully, live.

5. **Remove `col_offset` from the term** (now that the discipline bounds erase):
   field, action, opcode, interface slot, serial no-op. (This is the user's
   headline ask; it comes *after* the tracker proves out, not before.)

6. **Tab width in `tabs_buf`** (salvage C2's term part): `bool*`→`u8*`, store
   width, exact jump-back. Independent of the bound work.

7. **`tabs_buf` scroll/clear consistency** (salvage C3).

8. **Backspace row-wrap** (salvage C4) — now safe because the tracker bounds it.

9. **(Optional, separate) buffer correctness** per §6.3 Option B, if Q1 warrants
   and the user wants it. Heavy testing; its own risk budget.

10. **Tests** — unit where meaningful, but every commit ALSO live-verified.

Order rationale: restore correctness (1–4) before removing the old mechanism
(5), so there is never a window where the tree is "compiles + gtests pass" but
backspace is broken on hardware.

---

## 8. Testing strategy — MANDATORY, and why the last attempt fooled us

**The gtests cannot catch this class of bug.** `console_test.cpp` drives input
via `feed_key()` → the discipline **synchronously**; nothing runs `tty_read_int`,
so `input_ringbuf` is never drained and `ret` is always true. 304 green gtests
coexisted with a completely broken backspace. **Live verification is required
for every commit that touches the canonical input/echo/erase path.**

### 8.1 Live test harness (this works; use it)

```bash
./scripts/cmake_run -DBOOT_INTERACTIVE=0 -DMOD_fb=0 && make -j     # headless text mode
tmux kill-session -t tilck 2>/dev/null; rm -f /tmp/qmon
tmux new-session -d -s tilck -x 110 -y 40
tmux send-keys -t tilck \
  "bash build/run_multiboot_qemu -display curses -monitor unix:/tmp/qmon,server,nowait" Enter
until tmux capture-pane -t tilck -p | grep -q 'root@tilck'; do sleep 1; done
```

Send keys (RELIABLE — the QEMU **monitor `sendkey`** injects scancodes; this is
what `tests/runners/run_interactive_test` uses):

```bash
tmux send-keys -t tilck "cat" Enter; sleep 1
tmux send-keys -l -t tilck "abc"               # normal chars via curses stdin OK
echo "sendkey backspace" | nc -U /tmp/qmon     # backspace via monitor (0x7f)
tmux capture-pane -t tilck -p | sed -n '28,33p' | sed 's/ *$//'
```

Gotchas learned:
- **curses stdin does NOT reliably deliver special keys** (Backspace/Ctrl/DEL).
  Use the **monitor `sendkey`** for them. Normal printable chars via
  `tmux send-keys -l` are fine.
- The Backspace key = **0x7f** (= default VERASE). `sendkey ctrl-c` exits `cat`.
- `printk` debug goes to the VGA console and interleaves with `cat`; grep a tag
  (e.g. `DBG`). `printk` works in IRQ context here.
- Alternative the user suggested: drive via **QMP** (`-qmp unix:…`) with the
  `qmp_capabilities` + `send-key` handshake — more robust than HMP if needed.

### 8.2 Live cases (must all pass on real-ish kernel)

- `cat`: type `abc`, *pause*, 3× backspace → all three erased on screen; ENTER →
  `cat` returns empty line (buffer correct — depends on §6.3 outcome; at minimum
  the screen must be right and never eat the prompt).
- Backspace at column 0 with nothing typed → no-op, prompt untouched.
- Control char: in `cat`, the arrow-key / a `^V`-quoted control → caret echo →
  backspace clears both cells.
- Tab: type `a` then TAB then backspace → cursor returns exactly after `a`.
- Wrapped line (type >80 chars) → backspace across the wrap erases the tail of
  the previous row.
- Both **fb console** (`MOD_fb=1`) and **text mode** (`MOD_fb=0`). The user
  reported the bug in BOTH; verify BOTH.

### 8.3 Keep the gtests, but trust them only for the term-glyph math

Tab-width jump, scroll-shift of `tabs_buf`, wrap geometry, caret-pair erase math
— those ARE meaningfully unit-testable (they're pure term-buffer logic). The
discipline↔read↔term timing is NOT; that's live-only.

---

## 9. What to salvage from the first attempt

- **KEEP (term-side, correct):** tab width stored in `tabs_buf` (`bool`→`u8`),
  the exact jump-back backspace, `tabs_buf` scroll/clear consistency
  (`buf_copy_row`/`ts_buf_clear_row`/incr-row shift), the row-wrap term logic,
  and the test-harness `test_vi_move_cursor` clamp for the deferred-wrap cursor
  (`col == cols`). The `bool`→`u8` was a genuine latent bug fix (a `bool` can't
  hold a width).
- **REDO (discipline-side, wrong premise):** the `drop_last` rewrite (C1) and
  the `col_offset` removal mechanism (C2). Replace with §6.1/§6.2.
- **RE-ORDER:** remove `col_offset` only AFTER the discipline tracker is proven
  (§7 step 5), never before.

---

## 10. Key references (names; line numbers drift)

- `kernel/tty/tty_input.c`: `tty_keypress_handler_int`, `tty_send_keyevent`,
  `tty_handle_special_controls`, `tty_keypress_handle_canon_mode` (VERASE →
  `drop_last`), `tty_inbuf_write_elem` (ring write + echo; regular chars don't
  signal `input_cond`), `tty_inbuf_drop_last_written_elem`, `tty_keypress_echo`
  (ECHOE→VERASE branch; caret branch), `tty_read_int`, the drain loop +
  `KRN_TIMER_HZ/4` wait, `tty_internal_read_single_char_from_kb` (cooks into
  `eh->read_buf`), `tty_internal_should_read_return`.
- `kernel/tty/tty_ioctl.c`: default termios (`ECHOE|ECHOCTL`, `VERASE=0x7f`).
- `modules/console/console_def_state.c.h`: `tty_def_state_verase`
  (`del_prev_char`), `tty_def_state_backspace` (`move_cur_rel`, for `\b`),
  `tty_update_default_state_tables` (registers `def_state_funcs[VERASE]`).
- `modules/console/video_term.c`: `term_internal_write_backspace`
  (col_offset floor today), `term_internal_write_tab`, `term_internal_delete_last_word`
  (VWERASE; loops backspace), `buf_copy_row`, `ts_buf_clear_row`,
  `term_internal_incr_row`, `vterm_get_curr_col`.
- `modules/console/term_actions.c.h`: `term_action_del` (TERM_DEL_PREV_CHAR →
  backspace), end of `term_action_write` (the single `move_cursor` flush).
- `modules/console/vterm_struct.h`: `col_offset`, `tabs_buf`.
- `include/tilck/kernel/term.h`: the term interface (where `set_col_offset`
  lived; add `get_curr_col`).
- `tests/runners/run_interactive_test`: monitor `sendkey` usage (proven key
  injection).

---

## 11. Lessons / gotchas (don't relearn these)

1. **`input_ringbuf` is NOT the line buffer.** It's drained into per-handle
   `eh->read_buf` within ~250 ms. Never bound or size an erase by it.
2. **`col_offset` (or an equivalent reliable column) is load-bearing.** The
   visible erase must be bounded by the input-start column, not by buffer
   occupancy. Removing it without a replacement breaks the prompt boundary.
3. **gtests give false confidence here.** They drive the discipline
   synchronously; they cannot reproduce the drain/async/echo timing. **Live-test
   every canonical-path change** (§8) — in BOTH fb and text console.
4. **The visible backspace is driven by echoing the VERASE byte to the term**
   (ECHOE branch → `def_state_verase` → `del_prev_char`), not by the
   `input_ringbuf` unwrite. The unwrite is best-effort buffer cleanup.
5. **Backspace key = 0x7f = default VERASE.** `'\b'` (0x08) is a separate,
   non-destructive cursor-left in the term.
6. **QEMU monitor `sendkey` is the reliable key-injection path** for headless
   testing; curses stdin drops special keys.
7. `tabs_buf` as `bool` silently truncates a stored width to 1 — use `u8`.
8. Before changing the canonical reader's drain/timeout behaviour, re-read the
   `kb_worker_thread` missed-wakeup comment in `tty_read_int` and re-test
   riscv64-without-KVM boot (timeout interacts with boot timing).
