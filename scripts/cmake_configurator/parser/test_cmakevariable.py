import unittest
from cmake_variable import cmake_variable

class test_cmake_cache_variable(unittest.TestCase):
    def test_var_creation(self):
      comment: str = "Build unit tests for the target arch"
      line: str = "ARCH_GTESTS:BOOL=OFF"
      idx: int = 0
      var = cmake_variable(line, comment, idx)
      self.assertEqual(var.name, "ARCH_GTESTS") \
         and self.assertEqual(var.val == "OFF") \
         and self.assertEqual(var.type == "BOOL")
