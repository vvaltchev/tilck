# SPDX-License-Identifier: BSD-2-Clause

import os
import sys
from .lang_aux import Const, ReloadAsConstModule

def env_bool(x):
   return Const(os.environ.get(x, '0') == '1')

def env_int(x, val):
   return Const(int(os.environ.get(x, str(val))))

VM_MEMORY_SIZE_IN_MB = env_int('TILCK_VM_MEM', 128)
GEN_TEST_DATA = env_bool('GEN_TEST_DATA')
IN_TRAVIS = env_bool('TRAVIS')
IN_CIRCLECI = env_bool('CIRCLECI')
IN_AZURE = env_bool('AZURE_HTTP_USER_AGENT')
DUMP_COVERAGE = env_bool('DUMP_COV')
REPORT_COVERAGE = env_bool('REPORT_COV')
VERBOSE = env_bool('VERBOSE')
IN_ANY_CI = Const(IN_TRAVIS.val or IN_CIRCLECI.val or IN_AZURE.val)

ReloadAsConstModule(__name__)


