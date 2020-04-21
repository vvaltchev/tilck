# SPDX-License-Identifier: BSD-2-Clause
# pylint: skip-file
#
# NOTE: this file, as all the others in this directory, run in the same global
# context as their runner (run_interactive_test).

send_string_to_vm(r"fbtest{ret}")
s = vm_take_stable_screenshot()
send_string_to_vm(r"{ret}")

actual = img_convert(s, "png")
expected = os.path.join(INTERACTIVE_EXP, "fbtest.png")

if not filecmp.cmp(actual, expected, False):
   raise IntTestScreenshotNoMatchFailure(
      "<image: {}>".format(actual),
      "<image: {}>".format(expected)
   )

