#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=missing-function-docstring, bad-indentation

import os
import sys
import subprocess

from typing import List

TIMEOUTS = {
   'selftest':    15,
   'shellcmd':    60,
   'interactive': 120,
}

DEFAULT_ARCH = 'i386'


class TestResult:

   def __init__(self, name, passed=False):
      self.name = name
      self.passed = passed

   def __str__(self):
      return '{}: {}'.format(self.name, 'PASSED' if self.passed else 'FAILED')


def find_tests(path: str, suffix: str = '.sh') -> List[str]:

   res = []

   for name in sorted(os.listdir(path)):

      if not name.endswith(suffix):
         continue

      res.append(os.path.join(path, name))

   return res


def run_one(cmd, timeout):

   assert(timeout > 0)

   try:

      p = subprocess.run(cmd,
                         stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT,
                         timeout=timeout)

   except subprocess.TimeoutExpired:
      return TestResult(cmd[0])

   return TestResult(cmd[0], p.returncode == 0)


def report(results, verbose):

   failed = [r for r in results if not r.passed]

   if verbose:
      for r in results:
         print(r)

   if failed:

      print('Failed tests:')

      for r in failed:
         print('   ' + r.name)

   else:
      print('All tests passed')

   return len(failed)


def main():

   if len(sys.argv) < 2:
      print('Syntax: {} <dir>'.format(sys.argv[0]))
      sys.exit(1)

   tests = find_tests(sys.argv[1])
   results = [
      run_one([t], TIMEOUTS['shellcmd'])
      for t in tests
   ]

   sys.exit(report(results, verbose=False))


if __name__ == '__main__':
   main()
