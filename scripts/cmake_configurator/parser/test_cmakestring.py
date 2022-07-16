import unittest
from cmake_string import cmake_string

class test_cmake_string(unittest.TestCase):
   def __init__(self, methodName: str = ...) -> None:
      super().__init__(methodName)
      self.slash_comment: str  = "//Build unit tests for the target arch"
      self.not_comment: str = "Build unit tests for the target arch"
      self.hashtag_comment: str = "#Build unit tests for the target arch"

   def test_comment_string(self):
      string = cmake_string(self.slash_comment)
      self.assertEqual(string.user_comment(), self.slash_comment[2:])

   def test_is_comment_slash(self):
      string = cmake_string(self.slash_comment)
      self.assertTrue(string.is_comment())

   def test_is_comment_hashtag(self):
      string = cmake_string(self.hashtag_comment)
      self.assertTrue(string.is_comment())

   def test_is_not_comment(self):
      string = cmake_string(self.not_comment)
      self.assertFalse(string.is_comment())

   def test_empty(self):
      string = cmake_string(self.slash_comment)
      self.assertFalse(string.is_empty())

   def test_is_not_variable(self):
      string = cmake_string(self.slash_comment)
      self.assertFalse(string.is_variable())

   def test_is_not_variable_no_type(self):
      string = cmake_string("BOOTLOADER_EFI:=")
      self.assertFalse(string.is_variable())

   def test_is_variable(self):
      string = cmake_string("BOOTLOADER_EFI:BOOL=ON")
      self.assertTrue(string.is_variable())

   def test_user_comment_empty(self):
      string = cmake_string("")
      self.assertEqual(string.user_comment(), "")
