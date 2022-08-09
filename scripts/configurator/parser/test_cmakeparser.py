from unittest import TestCase
from cmake_parser import parse_rows, row_type

file_lines_generic = """
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

//Path to a program.
//Example Internal variable
CMAKE_ADDR2LINE:FILEPATH=/usr/bin/addr2line

CMAKE_ADDR2LINE:FILEPATH=/usr/bin/addr2line
"""

expected_comments = [
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
   splitted_lines = file_lines_generic.splitlines()
   def test_serialization(self):
      rows = parse_rows(self.splitted_lines)
      for row, line in zip(rows, self.splitted_lines):
         self.assertEqual(row.serialize(), line)

   def test_variable_comments(self):
      rows = parse_rows(self.splitted_lines)
      var_rows = [row for row in rows if row.row_type == row_type.VARIABLE]
      for row, comment in zip(var_rows, expected_comments):
         self.assertEqual(row.comment, comment)
