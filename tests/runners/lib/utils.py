# SPDX-License-Identifier: BSD-2-Clause

import re
import os
import sys
import subprocess
from enum import Enum

# Constants
test_types = ['selftest', 'shellcmd']

class Fail(Enum):
   success                 = 0
   invalid_args            = 1
   reboot                  = 2
   timeout                 = 3
   panic                   = 4
   shell_no_zero_exit      = 5
   gcov_error              = 6
   shell_unknown_exit_code = 7
   invalid_build_config    = 8

def getFailByCode(err_code):

   for f in Fail:
      if f.value == err_code:
         return f

   return None

def is_cmake_opt_enabled(opt):
   return opt.lower() in ["on", "1", "true", "yes", "y"]
