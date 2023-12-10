# SPDX-License-Identifier: BSD-2-Clause

from unittest import TestCase
from unittest.mock import patch
from cmake_parser import parse_rows, row_type, write_file

test_file1 = """
# KEY:TYPE=VALUE
# KEY is the name of a variable in the cache.
# TYPE is a hint to GUIs for the type of VALUE, DO NOT EDIT TYPE!.
# VALUE is the current value for the KEY.

########################
# EXTERNAL cache entries
########################

//Build unit tests for the target arch
ARCH_GTESTS:BOOL=OFF

//Build the EFI bootloader
BOOTLOADER_EFI:BOOL=ON

//Build the legacy bootloader
BOOTLOADER_LEGACY:BOOL=ON

//Make the bootloader to poison all the available memory
BOOTLOADER_POISON_MEMORY:BOOL=OFF

//Have user-interactive commands in the bootloaders
BOOT_INTERACTIVE:BOOL=ON

//Path to a program.
CMAKE_ADDR2LINE:FILEPATH=/usr/bin/addr2line

//Example Internal variable
CMAKE_AR:INTERNAL=/usr/bin/ar

//Path to a program.Example Internal variable
CMAKE_ADDR2LINE:FILEPATH=/usr/bin/addr2line

CMAKE_ADDR2LINE:FILEPATH=/usr/bin/addr2line
"""

test_file1_lines = test_file1.splitlines()

expected_comments_file1 = [
   "Build unit tests for the target arch",
   "Build the EFI bootloader",
   "Build the legacy bootloader",
   "Make the bootloader to poison all the available memory",
   "Have user-interactive commands in the bootloaders",
   "Path to a program.",
   "Example Internal variable",
   "Path to a program.Example Internal variable",
   ""
]

class test_cmake_parser_lines(TestCase):

   def test_serialization(self):
      rows = parse_rows(test_file1_lines)
      for row, line in zip(rows, test_file1_lines):
         self.assertEqual(row.serialize(), line)

   def test_variable_comments(self):
      rows = parse_rows(test_file1_lines)
      var_rows = [row for row in rows if row.row_type == row_type.VARIABLE]
      for row, comment in zip(var_rows, expected_comments_file1):
         self.assertEqual(row.comment, comment)

class test_cmake_parser_io(TestCase):

   def test_write_file(self):

      filebuf = ""

      class fake_file_handle:
         def write(self, data):
            nonlocal filebuf
            filebuf += data

      class fake_open:

         def __init__(self, filepath, mode):
            pass

         def __enter__(self):
            return fake_file_handle()

         def __exit__(self, type, value, traceback):
            pass

      rows = parse_rows(test_file1_lines)

      with patch('builtins.open', new = fake_open):
         write_file(None, rows)

      self.assertEqual(filebuf, test_file1)
