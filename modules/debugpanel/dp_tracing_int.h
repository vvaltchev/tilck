#pragma once

#define REND_BUF_SZ                              256
extern char *rend_bufs[6];
extern int used_rend_bufs;

void
dp_handle_syscall_event(struct trace_event *e);
