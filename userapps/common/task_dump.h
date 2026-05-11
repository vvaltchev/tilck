/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Shared task-table dumping infrastructure.
 *
 * Both the dp Tasks panel (screen_tasks.c, panel mode with selection
 * highlighting and scrolling) and the standalone `tracer` binary
 * (screen_tracing.c, plain-text dump on 'p' / 'P') need a way to
 * fetch the kernel task table and render it. The buffer + the
 * column-format machinery + the state-character helper sit here so
 * both binaries can link the same code without dragging in each
 * other's UI.
 *
 * The plain-text dump (`dp_dump_task_list_plain`) is implemented
 * here too — it uses term_write only and so doesn't depend on the
 * dp panel's row-counter macro (dp_writeln). The panel-mode render
 * with selection highlighting + scroll geometry stays in
 * screen_tasks.c, where its own static `row` lives.
 */

#pragma once

#include <stdbool.h>
#include <tilck/common/dp_abi.h>

#define MAX_DP_TASKS         512
#define MAX_EXEC_PATH_LEN     34

/*
 * Task state bytes stored in dp_task_info.state — values from
 * <tilck/kernel/sched.h> (TASK_STATE_*). Kept in sync by the
 * kernel-side TILCK_CMD_DP_GET_TASKS handler.
 */
#define TS_INVALID    0
#define TS_RUNNABLE   1
#define TS_RUNNING    2
#define TS_SLEEPING   3
#define TS_ZOMBIE     4

extern struct dp_task_info dp_tasks_buf[MAX_DP_TASKS];
extern int dp_tasks_count;

enum task_dump_str_t {
   TDS_HEADER,
   TDS_ROW_FMT,
   TDS_HLINE,
};

/*
 * Re-fetch the task table from the kernel into dp_tasks_buf.
 * Returns 0 on success, -1 if the TILCK_CMD failed.
 */
int dp_tasks_refresh(void);

/* Format the state byte (+ stopped/traced flags) as 1-3 chars. */
void state_to_str(char *out, unsigned char state, bool stopped, bool traced);

/* Get one of the table format strings (lazily built on first call). */
const char *task_dump_str(enum task_dump_str_t t);

/*
 * Plain-text dump of the task list to stdout via term_write.
 * Used by dp's `ps` mode and by the tracer's 'p' / 'P' keys.
 * Refreshes the task table first.
 *   kernel_tasks=true  → also include kthreads (tracer 'P').
 *   kernel_tasks=false → user processes only (tracer 'p' or ps).
 */
void dp_dump_task_list_plain(bool kernel_tasks);
