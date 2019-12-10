/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#define TILCK_CMD_SYSCALL    499

enum tilck_cmd {

   TILCK_CMD_RUN_SELFTEST        = 0,
   TILCK_CMD_GCOV_GET_NUM_FILES  = 1,
   TILCK_CMD_GCOV_FILE_INFO      = 2,
   TILCK_CMD_GCOV_GET_FILE       = 3,
   TILCK_CMD_QEMU_POWEROFF       = 4,
   TILCK_CMD_SET_SAT_ENABLED     = 5,

   /* Number of elements in the enum */
   TILCK_CMD_COUNT               = 6,
};
