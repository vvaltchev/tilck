# SPDX-License-Identifier: BSD-2-Clause
# pylint: skip-file
#
# NOTE: this file, as all the others in this directory, run in the same global
# context as their runner (run_interactive_test).

just_run_vim_and_exit()

do_interactive_actions(
   "vim2",
   [
      r"cd /usr/lib/vim/samples{ret}vim perl.pl{ret}",
      r"{esc}:open python.py{ret}",
      r"{esc}:open ruby.rb{ret}",
      r"{esc}:open shell.sh{ret}",
      r"{esc}:q{ret}cd /{ret}",
   ]
)
