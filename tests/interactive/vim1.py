# SPDX-License-Identifier: BSD-2-Clause
# pylint: skip-file
#
# NOTE: this file, as all the others in this directory, run in the same global
# context as their runner (run_interactive_test).

just_run_vim_and_exit()

do_interactive_actions(
   "vim1",
   [
      r"vim /usr/lib/vim/samples/numbers.txt{ret}",
      r"{esc}:29{ret}",
      r"{down}",
      r"{down}",
      r"{down}",
      r"{esc}:3{ret}",
      r"{up}",
      r"{up}",
      r"{esc}:q{ret}",
   ]
)
