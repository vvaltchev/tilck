/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

void gcov_dump_coverage(void);

int tilck_sys_gcov_get_file_count(void);
int tilck_sys_gcov_get_file_info(int fn,
                                 char *user_fname_buf,
                                 u32 fname_buf_size,
                                 u32 *user_fsize);
int tilck_sys_gcov_get_file(int fn, char *user_buf);
