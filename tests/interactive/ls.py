# SPDX-License-Identifier: BSD-2-Clause
# pylint: skip-file
#
# NOTE: this file, as all the others in this directory, run in the same global
# context as their runner (run_interactive_test).

send_to_vm_and_find_text(r"ls -l /{ret}", False, ["bin", "dev", "usr"])
