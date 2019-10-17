# SPDX-License-Identifier: BSD-2-Clause
import sys
from enum import Enum

# Constants

class Fail(Enum):
   success              = 0
   invalid_args         = 1
   reboot               = 2
   timeout              = 3
   panic                = 4
   shell_no_zero_exit   = 5
   gcov_error           = 6

def getFailByCode(err_code):

   for f in Fail:
      if f.value == err_code:
         return f

   return None

test_types = ['selftest', 'shellcmd']

# Constants coming from CMake (this file gets pre-processed by CMake)
KERNEL_FILE = r'@KERNEL_FILE@'
BUILD_DIR = r'@CMAKE_BINARY_DIR@'

def raw_print(msg):
   sys.stdout.buffer.write(msg.encode('utf-8'))
   sys.stdout.buffer.write('\n'.encode('utf-8'))

def msg_print(msg):
   raw_print("[system test runner] {}".format(msg))
