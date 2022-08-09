from unittest import TestCase
from cmake_row import cmake_row, row_type

class test_cmake_row(TestCase):
   commentline = ""
   filepath_var_valid = "CMAKE_ADDR2LINE:FILEPATH=/usr/bin/addr2line"
   noname_var = ":FILEPATH=/usr/bin/addr2line"
   notype_var = "CMAKE_ADDR2LINE:=/usr/bin/addr2line"
   bool_var = "CMAKE_ADDR2LINE:BOOL=ON"

   def test_row_creation(self):
      var_name = "CMAKE_ADDR2LINE"
      row = cmake_row(self.filepath_var_valid, 0)
      with self.subTest():
         self.assertEqual(row.name, var_name)
      with self.subTest():
         self.assertEqual(row.row_type, row_type.VARIABLE)
      with self.subTest():
         self.assertEqual(row.get_val(), "/usr/bin/addr2line")
      with self.subTest():
         self.assertEqual(row.row_number, 0)

   def test_slash_comment_row(self):
      row = cmake_row("//" + self.filepath_var_valid, 0)
      with self.subTest():
         self.assertEqual(row.row_type, row_type.SLASH_COMMENT)
      with self.subTest():
         self.assertEqual(row.serialize(), "//" + self.filepath_var_valid)

   def test_pound_comment_row(self):
      row = cmake_row("#" + self.filepath_var_valid, 0)
      with self.subTest():
         self.assertEqual(row.row_type, row_type.POUND_COMMENT)
      with self.subTest():
         self.assertEqual(row.serialize(), "#" + self.filepath_var_valid)

   def test_noname_row(self):
      self.assertRaises(ValueError,  cmake_row, self.noname_var, 0)

   def test_empty_line(self):
      row = cmake_row("", 0)
      self.assertEqual(row.row_type, row_type.EMPTY)

   def test_notype_row(self):
      self.assertRaises(ValueError, cmake_row, self.notype_var, 0)

   def test_bool_row(self):
      """
      we use assertEqual instead of assertTrue to avoid false positives with
      casting any other value to bool.
      """
      row = cmake_row(self.bool_var, 0)
      with self.subTest():
         self.assertEqual(row.val.value, True)
      with self.subTest():
         self.assertEqual(row.serialize(), self.bool_var)

   def test_emtpy_row(self):
      row = cmake_row("", 0)
      self.assertEqual("", row.serialize())
