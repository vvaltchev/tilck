import unittest
from cmake_row import cmake_row
from cmake_variable import cmake_variable

comment: str = "Build unit tests for the target arch"
bool_line: str = "ARCH_GTESTS:BOOL=OFF"
internal_line: str = "CMAKE_AR:INTERNAL=/usr/bin/ar"
arch_row: cmake_row = cmake_row(bool_line, 0)

class test_cmake_cache_variable(unittest.TestCase):

   def test_var_creation(self):
      var = cmake_variable(arch_row, comment)
      with self.subTest():
         self.assertEqual(var.row.name, "ARCH_GTESTS")
      with self.subTest():
         self.assertEqual(var.row.val, "OFF")
      with self.subTest():
         self.assertTrue(var.row.var_type, "BOOL")

   def test_input_valid_bool(self):
      var = cmake_variable(arch_row, comment)
      with self.subTest():
         self.assertTrue(var.is_valid("true"))
      with self.subTest():
         self.assertFalse(var.is_valid("te"))

   def test_input_valid_internal(self):
      arch_row = cmake_row(internal_line, 0)
      var  = cmake_variable(arch_row, "")
      self.assertFalse(var.is_valid(""))
