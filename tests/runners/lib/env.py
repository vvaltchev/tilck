# SPDX-License-Identifier: BSD-2-Clause

import os

VM_MEMORY_SIZE_IN_MB = int(os.environ.get('TILCK_VM_MEM', '128'))
GEN_TEST_DATA = os.getenv('GEN_TEST_DATA', '0') == '1'

in_travis = os.environ.get('TRAVIS', False)
in_circleci = os.environ.get('CIRCLECI', False)
in_azure = os.environ.get('AZURE_HTTP_USER_AGENT', False)
dump_coverage = os.environ.get('DUMP_COV', False)
report_coverage = os.environ.get('REPORT_COV', False)
verbose = os.environ.get('VERBOSE', False)
in_any_ci = in_travis or in_circleci or in_azure
