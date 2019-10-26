/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#define PIPE_BUF_SIZE   4096

struct pipe;

struct pipe *create_pipe(void);
void destroy_pipe(struct pipe *p);
fs_handle pipe_create_read_handle(struct pipe *p);
fs_handle pipe_create_write_handle(struct pipe *p);
