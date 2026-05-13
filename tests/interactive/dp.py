# SPDX-License-Identifier: BSD-2-Clause
# pylint: skip-file
#
# NOTE: this file, as all the others in this directory, run in the same global
# context as their runner (run_interactive_test).

send_to_vm_and_find_text(r"dp{ret}", True, [
   "TilckDebugPanel",
   "Config",
   "debugchecks",
])

send_to_vm_and_find_text(r"2", True, [
   "Runtime",
   "term_rows",
   "hypervisor",
])

send_to_vm_and_find_text(r"3", True, [
   "START",
   "END",
   "usable physical mem",
])

send_to_vm_and_find_text(r"4", True, [
   "Usable",
   "Used",
   "vaddr",
])

send_to_vm_and_find_text(r"5", True, [
   "pid",
   "tty",
   "cmdline",
])

send_to_vm_and_find_text(r"6", True, [
   "counters",
])

send_to_vm_and_find_text(r"7", True, [])
send_string_to_vm(r"q")
