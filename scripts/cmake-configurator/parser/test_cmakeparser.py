from cmake_parser import cmake_parser
from unittest import TestCase

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
   parser = cmake_parser()

   def test_parser_read_and_write(self):
      vars = self.parser.lines_to_vars(file_lines_generic.splitlines())
      new_lines = self.parser.vars_to_lines(vars)
      self.assertEqual(file_lines_generic.splitlines(), new_lines)

   def test_single_comments(self):
      vars = self.parser.lines_to_vars(file_lines_generic.splitlines())
      for var, comment in zip(vars, expected_comments):
         self.assertEqual(var.comment, comment)
