# Master `dp` feature spec — full inventory

Authoritative reference for the userspace dp port. Built by reading every
file under `modules/debugpanel/` on `master` (commit before the userspace
work started). Anything **not in this list is NOT in master** — but
everything below MUST work in the userspace port.

The "Options" panel is exempt from the strict-parity requirement (user
explicitly said its new look is fine); every other panel and the tracer
must match master byte-for-byte modulo cosmetic divergence flagged here.

---

## 1. Top-level shell (`dp.c::dp_common_entry`)

- Entry points (registered via `register_tilck_cmd`):
  - `TILCK_CMD_DEBUG_PANEL`   → `dp_default_entry`        (full panel)
  - `TILCK_CMD_TRACING_TOOL`  → `dp_direct_tracing_mode_entry` (tracer only)
  - `TILCK_CMD_PS_TOOL`       → `dp_ps_tool`              (one-shot ps)
  - `TILCK_CMD_DEBUGGER_TOOL` → `dp_mini_debugger_tool`   (panic only)
- Top-of-screen header: `1[Options] 2[MemMap] 3[Heaps] 4[Tasks] 5[IRQs] 6[MemChunks] q[Quit]`
  - Selected screen rendered as `<index>` BR_WHITE + `[label]` REVERSE_VIDEO.
  - Numbers `1..N` jump to the matching screen.
  - `q` quits the panel (restores TTY mode + alt buffer).
  - `Ctrl+C` quits as well.
- Footer: `[ TilckDebugPanel ]` left-anchored (yellow), `[rows X-Y of Z]`
  right-anchored (BR_RED).
- Switches to the alt video buffer in video TTYs (so the user's prompt
  is restored after exit), uses raw write in serial mode.
- Terminal configured to raw mode + cursor hidden + non-blocking read.

## 2. Page navigation

- `PAGE_DOWN` / `PAGE_UP` move `dp_ctx->row_off` ±1 (clamped to
  `[0, row_max - screen_rows]` upper bound, but only by the
  `if (... < row_max)` check before increment).
- Each panel maintains its own row_off + row_max independently.
- `redraw_screen` clamps the rendered range and shows
  `[rows row_off+1 - min(row_off+screen_rows,row_max)+1 of row_max+1]`.

## 3. Modal message system

- `modal_msg = "..."` raises a centered modal dialog over the current
  screen with text + "Press ANY key to continue".
- Used by Tasks panel for "Killing kernel threads or pid 1 is not allowed"
  etc.
- `skip_next_keypress` is set when a modal is shown so the dismissal
  keypress doesn't trigger a screen action.

## 4. Screens (in master enum order)

### 4.1 Options (index = 0)

Two columns:
- **Left ("Build-time")** with green border:
  - Sections: `Main`, `Kernel modules`, `Modules config`,
    `Enabled by default`, `Disabled by default`, `Other`.
  - Lists DUMP_BOOL_OPT / DUMP_INT_OPT / DUMP_STR_OPT for ~50 compile
    flags. Booleans rendered green when 1, default-color when 0.
- **Right ("Run-time")** with green border:
  - Sections: `Main`, `Console`, `System clock`.
  - HYPERVISOR (in_hypervisor()), TERM_ROWS/COLS, USE_FRAMEBUFFER,
    FB_OPT_FUNCS / FB_RES_X / FB_RES_Y / FB_BPP / FB_FONT_W / FB_FONT_H,
    TTY_COUNT (kopt_ttys), CLK_IN_RS, CLK_IN_FULL_RS,
    CLK_FULL_RS_CNT, CLK_FULL_RS_FAIL, CLK_FULL_RS_SUCC,
    CLK_FULL_RS_ADG1, CLK_RESYNC_CNT.
- **No keys.** Pure read-only display.

**Parity exemption: user is fine with our current Options panel.**

### 4.2 MemMap (index = 1)

- Calls `dump_global_mem_stats` (under disable_preemption):
  Total usable (KB + MB), Used by kmalloc, Used by initrd,
  Used by kernel text+data, Tot used.
- `dump_mmap`: header `START | END | (T, Extr)`, then one row per
  `mem_region`: hex range + type + extra flags + size in KB.
- `dump_var_mtrrs` (x86 only): default mem type, then one row per
  variable MTRR with base, mem type, size KB. "MTRRs: not supported on
  this CPU" if `get_var_mttrs_count() == 0`.
- **No keys.**

### 4.3 Heaps (index = 2)

- `on_dp_enter` snapshots current per-heap allocated bytes into
  `heaps_alloc[]` and computes `tot_usable_mem_kb`, `tot_used_mem_kb`,
  `tot_diff` (delta vs baseline saved on previous `on_dp_exit`).
- Top-left: `Usable: %u KB`, `Used: %u KB (%u%%)`, `Diff: signed_color +/-N KB [N B]`.
- Top-right: `[ Small heaps ]` with `count: tot [peak: peak]`, `non-full: tot [peak: peak]`.
- Table header: `H# | R# | vaddr | size | used | MBS | diff` then GFX_ON
  separator.
- One row per heap: H#, region or "--", vaddr, size (KB or MB), used
  fraction `xxx.x%`, MBS (min block size), per-heap diff (signed,
  colored, B or KB).
- `on_dp_exit` updates the baseline.
- **No keys.**

### 4.4 Tasks (index = 3)

Two modes (`dp_tasks_mode_default`, `dp_tasks_mode_sel`).

**Default mode keys:**
- `<ENTER>` → enter selection mode.
- `r`        → refresh.
- `Ctrl+T`   → enter tracing screen (or modal "tracing module not built-in").

**Selection mode keys:**
- `ESC`      → back to default mode.
- `r`        → refresh.
- `t`        → toggle `task.traced` on selected (forbidden for self,
   kthreads, pid 1; modal_msg if so).
- `k`        → SIGKILL selected (forbidden for self, kthreads ≥
   KERNEL_TID_START, and pid 1; modal_msg if so).
- `s`        → SIGSTOP (forbidden for self, kthreads).
- `c`        → SIGCONT (silently no-op for off-limit).
- `Ctrl+T`   → enter tracing screen.
- `UP`/`DOWN`→ move sel cursor (sel_index ± 1, clamped to max_idx).

**Action menu rendered in panel header:**
- Default: `<ENTER>: select mode | r: refresh | Ctrl+T: tracing mode`
- Sel:     `ESC: exit select | r: refresh | Ctrl+T: tracing mode | t: trace task` then
          `k: kill | s: stop | c: continue` (two lines).

**Render flow:**
- Always shows kernel threads too (`kernel_tasks=true`), as
  `<wth:name(prio)>` for worker threads or `<name>` for kthreads.
- Skips main kernel task (TID == KERNEL_TID_START).
- State string: `r/R/s/Z/?` (+`S` if stopped) (+`t` if traced).
- Selected row → REVERSE_VIDEO via `dp_reverse_colors()`/`dp_reset_attrs`.

**ps mode:**
- Same renderer, plain-text, no UI loop. Filters out kthreads
  (kernel_tasks=false).

### 4.5 IRQs (index = 4)

- `Slow timer irq handler counter:` (only if KRN_TRACK_NESTED_INTERR).
- `Spurious IRQ count: N (N/sec)` (or just N if ticks < KRN_TIMER_HZ).
- `Unhandled IRQs count table` (only if any > 0). One row per nonzero
  vector: `IRQ #%3u: %3u unhandled`.
- `Unmasked IRQs:` followed by `#0 #1 ... #15` for unmasked legacy IRQs.
- **No keys.**

### 4.6 MemChunks (index = 5) — KRN_KMALLOC_HEAVY_STATS only

If `KRN_KMALLOC_HEAVY_STATS` is off:
- Renders one line: `Not available: recompile with KRN_KMALLOC_HEAVY_STATS=1`.

If on:
- Allocates `chunks_arr` lazily on first enter (16 KB shrinking on
  failure, `panic` if all four allocs fail).
- `on_dp_enter` snapshots `debug_kmalloc_chunks_stats_*` into the
  array, computes `lf_allocs`, `lf_waste`, `chunks_count`. Default sort
  is `'s'` (size).
- Top stats: `Chunk sizes count: N sizes`, `Lifetime data allocated:
  KB/MB [actual: KB/MB]`, `Lifetime max data waste: KB/MB (X.Y%)`.
- Sort menu: `Order by: size, count, waste, waste (%)` with selected
  letter highlighted in BR_WHITE REVERSE_VIDEO.
- Table: `Size | Count | Max waste | Max waste (%)` with the selected
  sort column highlighted (BR_WHITE REVERSE_VIDEO).
- One row per chunk size.
- **Keys**: `s` (size), `c` (count), `w` (waste), `t` (waste %).
  Each triggers an in-place `insertion_sort_generic` and ui_need_update.

## 5. Tracer screen — `dp_tracing.c::dp_tracing_screen`

This is the **biggest gap** in the userspace port today.

Two states: **banner** (waiting for command) and **live** (events flowing).

### 5.1 Banner UI (`tracing_ui_msg`)

```
Tilck syscall tracing (h: help)
│ Always ENTER+EXIT: ON/OFF │ Big bufs: ON/OFF  │ #Sys traced: N │ #Tasks traced: N │
│ Printk lvl: N
│ Trace expr: <syscall filter expr>
> _
```

- ON in green, OFF in red. N values in BR_BLUE. Filter in YELLOW.
- Refreshed after each command except live mode (where banner is
  overwritten by the event stream).

### 5.2 Help (`tracing_ui_show_help`, key = `h`)

Lists all commands:
- `o`: Toggle always enter + exit
- `b`: Toggle dump big buffers
- `e`: Edit syscalls wildcard expr [1]
- `k`: Set trace_printk() level
- `l`: List traced syscalls
- `p`: Dump user tasks list
- `P`: Dump full task list
- `t`: Edit list of traced PIDs
- `q`: Back to the debug panel
- `ENTER`: Start / stop tracing

Then notes [1]: `*` allowed once at end, `!` to negate, `,`/space
separators, `?` matches any single char. Example: `read*,write*,!readlink*`.

### 5.3 Per-key actions

**`o`**: `tracing_set_force_exp_block(!tracing_is_force_exp_block_enabled())`.
Echoes 'o', re-renders banner.

**`b`**: `tracing_set_dump_big_bufs_opt(!tracing_are_dump_big_bufs_on())`.
Echoes 'b', re-renders banner.

**`e`**: prompt `expr> ` (yellow), prefilled with current filter, line
edited via `dp_read_line` (blocking input on); `set_traced_syscalls(buf)`,
print `Invalid input` (red) on parse failure. Re-render banner.

**`k`**: prompt `Level [0, 100]: `, blocking line read, `tilck_strtol`
parse, range check 0..100. `tracing_set_printk_lvl(val)`.

**`l`**: dumps `Traced syscalls list` (yellow), then space-separated
list of all syscall names where `tracing_is_enabled_on_sys(i)` is true,
stripping the "sys_" prefix. Re-render banner.

**`p`**: `dp_dump_task_list(false, true)` — user tasks only,
plain-text. Re-render banner.

**`P`**: `dp_dump_task_list(true, true)` — kthreads too. Re-render
banner.

**`t`**: prompt `PIDs> `. Pre-fills with **currently traced TIDs**
(comma-separated) AND clears the traced flag on every task while
building that list (so the list is "edit me back"). User edits, hit
ENTER, parse comma/space-separated TIDs, `ti->traced = true` per match.
Print `Tracing N tasks` or `Invalid input`.

**`q`**: exit tracer (back to dp panel if entered from there, else
exit dp entirely).

**`ENTER`**: enter live mode, `tracing_set_enabled(true)`, run main
loop, on exit: `tracing_set_enabled(false)`, ask "Discard remaining?",
re-render banner.

### 5.4 Live loop (`dp_tracing_screen_main_loop`)

- Render `-- Tracing active --` (green).
- Loop: read 1 byte from input (non-blocking); on `q` clean exit, on
  `ENTER` "stop tracing" (returns true → re-render banner).
  Otherwise:
  - Call `read_trace_event(&e, KRN_TIMER_HZ/10)`.
  - If true: render the event (see 5.5).
- Trailing blank if last printk was incomplete.
- After loop: `-- Tracing stopped --` (red).

### 5.5 Event rendering (`dp_dump_tracing_event`)

For all events except `te_printk`: prefix `%05u.%03u [%05d] ` (sec.msec [tid]).
Then dispatch on event type:

#### te_sys_enter / te_sys_exit
`dp_handle_syscall_event(e)`:
- Strip `sys_` prefix from name.
- Lookup `syscall_info` from `tracing_metadata`.
- Render preamble:
  - `te_sys_enter`: `ENTER ` (BR_GREEN).
  - `te_sys_exit` w/ exp_block: `EXIT ` (BR_BLUE).
  - `te_sys_exit` w/o exp_block (and no si): `EXIT ` (BR_BLUE).
  - `te_sys_exit` w/o exp_block (and si exists): `CALL ` (YELLOW).
- If si is NULL: `name()`.
- If si is not NULL: `name(p1: rendered_p1, p2: rendered_p2, ...)`.
- For each param `p` in `si->params[0..n_params-1]`:
  - Skip if `p->invisible`.
  - Decide full-dump vs minimal-dump (`dp_should_full_dump_param`).
    - full if `p->kind == sys_param_in_out`,
    - or `te_sys_enter` and `kind == in`,
    - or `te_sys_exit` and (`!exp_block` or `kind == out`).
  - Full dump:
    - `tracing_get_slot(...)` → `data, data_size`. If success: call
      `p->type->dump(orig, data, data_size, helper, dst, REND_BUF_SZ)`.
    - Else: call `p->type->dump_from_val(orig, helper, dst, REND_BUF_SZ)`.
    - Fallback: `(raw) %p`.
  - Minimal dump (only for enter): use `ptype_voidp.dump_from_val`.
  - Skip if `rend_bufs[i][0] == 0`.
  - Render `pname` (MAGENTA) `: ` `rval` colored by `dp_get_esc_color_for_param`:
    - `"\""` and `ui_type_string` → RED
    - `&ptype_errno_or_val` and starts with `-` → WHITE_ON_RED
    - `ui_type_integer` → BR_BLUE
    - else: no color.
- For `te_sys_exit`: ` -> ` then `dp_dump_ret_val`:
  - If si == NULL: guess (≤1MB nonneg = BR_BLUE int, ≤1MB neg = WHITE_ON_RED `-<errno_name>`, else %p).
  - Else: `si->ret_type->dump_from_val(...)` rendered with the same param-color rule.
- Trailing `\r\n`.

#### te_printk
`dp_dump_trace_printk_event`:
- Skip leading single `\n` if prior line was complete.
- Detect truncation: if last bytes equal `TRACE_PRINTK_TRUNC_STR` ("{...}")
  OR buffer not NUL-terminated → render trailing red "{...}".
- Continuation (`continuation` flag): same tid + sys_time + in_irq as
  prior incomplete line → omit prefix and `LOG[lvl]: `, render in
  MAGENTA.
- Default: prefix + `LOG[%02d]: ` (yellow LOG) + body + (\r if line
  ended with \n, else \r\n if truncated, else nothing → marks as
  incomplete).
- If body starts with `*** ` → ATTR_BOLD.
- Empty body (`len == 0`) → skip the entire event.

#### te_signal_delivered
- Prefix + `GOT SIGNAL: ` (YELLOW) + `name[signum]\r\n`.
- Signal name from `get_signal_name(signum)`.

#### te_killed
- Prefix + `KILLED BY SIGNAL: ` (BR_RED) + `name[signum]\r\n`.

#### default
- Prefix + `<unknown event N>` (BR_RED) + `\r\n`.

### 5.6 Discard remaining events prompt
After live loop returns "stop tracing":
- If `tracing_get_in_buffer_events_count() > 0`:
  Print `Discard remaining N events in the buf? [Y/n] `.
  Wait for `y`/`n`/Enter (default Y).
  Drain ring with `read_trace_event_noblock`; if `n`, render each
  with `dp_dump_tracing_event`. Otherwise just consume.

### 5.7 Filter expression parsing
Implemented in `modules/tracing/tracing.c` — `set_traced_syscalls`.
Sub-exprs `,` or space separated. Each is `[!]<expr>` where expr can
have one trailing `*` (matches a prefix) and `?` (single-char wildcard).
Negative starts with `!` and removes from the set.

## 6. Misc shared utilities (termutil.c)

- `dp_writeln(fmt, ...)`, `dp_writeln2(fmt, ...)` — buffered scrolling lines.
- `dp_write(row, col, fmt, ...)` — absolute placement; updates `row_max`.
- `dp_draw_rect`, `dp_draw_rect_raw` — VT100 GFX-mode boxes.
- `dp_show_modal_msg` — centered dialog.
- `dp_reverse_colors`, `dp_reset_attrs` — used for selection highlight.
- `dp_sign_value_esc_color(v)` — green for positive, red for negative,
  default for zero (used in Heaps "Diff" column).

## 7. Color escape conventions

- E_COLOR_BR_GREEN: ENTER (syscall enter prefix).
- E_COLOR_BR_BLUE:  EXIT, integer values, "rows X-Y of Z" base color.
- E_COLOR_YELLOW:   "LOG", filter expr, prompts ("expr> ", "PIDs> "), CALL
                    prefix, panel header.
- E_COLOR_MAGENTA:  param names in syscall events; printk continuation body.
- E_COLOR_RED:      OFF state, errors ("Invalid input"), [1] footnote.
- E_COLOR_BR_RED:   KILLED BY SIGNAL prefix, truncation marker "{...}",
                    "rows X-Y of Z" right-anchored.
- E_COLOR_WHITE_ON_RED: errno values in retvals/params.
- E_COLOR_GREEN:    ON state, "Tracing active" banner, rect borders.
- E_COLOR_BR_WHITE: header digit, sort-by selected column.

---

# Userspace dp parity gap (as of dp-userspace HEAD)

Tracker for what's missing or different vs master. Tick boxes as they're
closed.

## Tracer (CRITICAL — must be 100% identical)

- [ ] `o` toggle force-exp-block — needs new TILCK_CMD_DP_TRACE_SET_FORCE_EXP_BLOCK
- [ ] `b` toggle dump-big-bufs — needs new TILCK_CMD_DP_TRACE_SET_DUMP_BIG_BUFS
- [ ] `k` set printk level — needs new TILCK_CMD_DP_TRACE_SET_PRINTK_LVL
- [ ] `l` list traced syscalls — needs new TILCK_CMD_DP_TRACE_GET_TRACED_BITMAP
- [ ] `p` dump user task list — wire to existing dp_dump_task_list-equivalent
- [ ] `P` dump full task list — same
- [ ] Help menu must list all 10 keys (currently 5)
- [ ] Per-syscall parameter rendering with `name: value` and color coding
  - Mirror modules/tracing/tracing_metadata.c, tracing_types.c,
    syscall_types.h, errno_names.c, ptype_buffer.c, ptype_iov.c into
    userapps/dp/.
  - Implement `tracing_get_slot` userspace equivalent reading
    `dp_syscall_event_data.saved_params` (the 176B opaque blob).
- [ ] EXIT vs CALL distinction (CALL when !exp_block && si exists)
- [ ] Errno-named retval (`-EINTR`, `-EBADF`, etc) instead of raw `-N`
- [ ] Signal-name in te_signal_delivered (`SIGINT[2]` not just `[2]`)
- [ ] Printk continuation logic (multi-line printk merging)
- [ ] Printk truncation handling ("{...}" marker)
- [ ] Printk leading-newline skip
- [ ] Printk "*** " bold detection
- [ ] "Discard remaining N events in the buf? [Y/n]" prompt
  - needs new TILCK_CMD_DP_TRACE_GET_IN_BUF_COUNT
- [ ] Edit-traced-PIDs ('t'): prefill with current traced set + clear
  - needs new TILCK_CMD_DP_TASK_GET_TRACED_TIDS or scan via existing
    DP_GET_TASKS

## Tasks panel

- [ ] `Ctrl+T` shortcut from anywhere in Tasks to enter tracer screen
- [ ] When entering tracer via Ctrl+T from Tasks, the tasks marked
  with `t` should be carried in (kernel-side state, already so —
  just need to call dp_run_tracer in-process).

## MemChunks

- [ ] Verify rendering with `KRN_KMALLOC_HEAVY_STATS=1` matches master.

## Sub-commands needed

| name                                        | id  | a1                | a2          | a3 | returns       |
|---------------------------------------------|-----|-------------------|-------------|----|---------------|
| TILCK_CMD_DP_TRACE_SET_FORCE_EXP_BLOCK      | 26  | int enable        | -           | -  | 0/-errno      |
| TILCK_CMD_DP_TRACE_SET_DUMP_BIG_BUFS        | 27  | int enable        | -           | -  | 0/-errno      |
| TILCK_CMD_DP_TRACE_SET_PRINTK_LVL           | 28  | int level         | -           | -  | 0/-errno      |
| TILCK_CMD_DP_TRACE_GET_TRACED_BITMAP        | 29  | u8 *buf           | ulong sz    | -  | nbytes/-errno |
| TILCK_CMD_DP_TRACE_GET_IN_BUF_COUNT         | 30  | -                 | -           | -  | count/-errno  |
| TILCK_CMD_DP_TASK_GET_TRACED_TIDS_AND_CLEAR | 31  | s32 *buf          | ulong max   | -  | count/-errno  |

## ABI additions

Bump `TILCK_CMD_COUNT` to 32. No change to dp_abi.h structs (the new
sub-commands take/return primitive types or already-defined buffers).
