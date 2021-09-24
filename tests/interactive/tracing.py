# SPDX-License-Identifier: BSD-2-Clause
# pylint: skip-file
#
# NOTE: this file, as all the others in this directory, run in the same global
# context as their runner (run_interactive_test).

# Switch to tty2
send_to_vm_and_find_text(r"{alt-f2}", True, [])

# Start the shell
send_to_vm_and_find_text(r"{ret}", True, [])

# Enter in the debug panel
send_to_vm_and_find_text(r"dp{ret}", True, [
   "TilckDebugPanel",
   "TERM_ROWS",
   "DEBUG_CHECKS",
])

# Select the tasks tab
send_to_vm_and_find_text(r"4", True, [
   "pid",
   "tty",
   "cmdline",
])

# Enter in select mode
send_to_vm_and_find_text(r"{ret}", True, [])

# Select the 2nd process (ASH on tty 1)
send_to_vm_and_find_text(r"{down}", True, [])

# Mark the process as TRACED
send_to_vm_and_find_text(r"{t}", True, [])

# Enter in tracing mode
send_to_vm_and_find_text(r"{ctrl-t}", True, [])

# Start the actual tracing
send_to_vm_and_find_text(r"{ret}", True, [])

# Switch back to tty1
send_to_vm_and_find_text(r"{alt-f1}", True, [])

# Just run `ls`
send_to_vm_and_find_text(r"ls{ret}", True, [])

# Switch back to tty2 and check the tracing output
send_to_vm_and_find_text(r"{alt-f2}", True, [
   "open",
   "wait4",
   "close",
   "poll",
   "writev",
])

# Stop the actual tracing
send_to_vm_and_find_text(r"{ret}", True, [])

# Exit from tracing mode
send_to_vm_and_find_text(r"q", True, [])

# Exit from the debug panel
send_string_to_vm(r"q")
