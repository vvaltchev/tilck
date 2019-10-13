# SPDX-License-Identifier: BSD-2-Clause
import sys

# Constants

# Fail codes:
FAIL_SUCCESS = 0
FAIL_INVALID_ARGS = 1
FAIL_REBOOT = 2
FAIL_TIMEOUT = 3
FAIL_PANIC = 4
FAIL_SHELL_NO_ZERO_EXIT = 5
FAIL_GCOV_ERROR = 6

error_codes_strings = {
   FAIL_SUCCESS: 'success',
   FAIL_INVALID_ARGS: 'invalid_arguments',
   FAIL_REBOOT: 'reboot',
   FAIL_TIMEOUT: 'timeout',
   FAIL_PANIC: 'panic',
   FAIL_SHELL_NO_ZERO_EXIT: 'shell_no_zero_exit',
   FAIL_GCOV_ERROR: 'gcov_issue'
}

# Constants coming from CMake (this file gets pre-processed by CMake)
KERNEL_FILE = r'@KERNEL_FILE@'
BUILD_DIR = r'@CMAKE_BINARY_DIR@'

def raw_print(msg):
   sys.stdout.buffer.write(msg.encode('utf-8'))
   sys.stdout.buffer.write('\n'.encode('utf-8'))

def msg_print(msg):
   raw_print("[system test runner] {}".format(msg))
