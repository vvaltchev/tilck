# SPDX-License-Identifier: BSD-2-Clause
# pylint: skip-file
#
# NOTE: this file, as all the others in this directory, run in the same global
# context as their runner (run_interactive_test).

send_string_to_vm("ls -l /")
send_single_key_to_vm("ret")
s = vm_take_stable_screenshot()
t = screenshot_to_text(s)

if t.find("KERNEL PANIC") != -1:
   raise KernelPanicFailure(t)

if t.find("bin") == -1 or t.find("dev") == -1 or t.find("usr") == -1:
   raise IntTestScreenTextCheckFailure(t)
