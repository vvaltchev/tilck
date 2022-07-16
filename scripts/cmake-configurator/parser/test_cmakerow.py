from unittest import TestCase
from cmake_row import cmake_row

class test_cmake_cache_variable(TestCase):
   commentline = ""
   filepath_var_valid = "CMAKE_ADDR2LINE:FILEPATH=/usr/bin/addr2line"
   noname_var = ":FILEPATH=/usr/bin/addr2line"
   notype_var = "CMAKE_ADDR2LINE:=/usr/bin/addr2line"

   def test_row_creation(self):
      var_name = "CMAKE_ADDR2LINE"
      row = cmake_row(self.filepath_var_valid, 0)
      with self.subTest():
         self.assertEqual(row.name, var_name)
      with self.subTest():
         self.assertEqual(row.var_type, "FILEPATH")
      with self.subTest():
         self.assertEqual(row.type, row.VARIABLE)
      with self.subTest():
         self.assertEqual(row.val,  "/usr/bin/addr2line")
      with self.subTest():
         self.assertEqual(row.row_number, 0)

   def test_slash_comment_row(self):
      row = cmake_row("//" + self.filepath_var_valid, 0)
      with self.subTest():
         self.assertEqual(row.type, row.SLASH_COMMENT)

   def test_hashtag_comment_row(self):
      row = cmake_row("#" + self.filepath_var_valid, 0)
      with self.subTest():
         self.assertEqual(row.type, row.HASHTAG_COMMENT)

   def test_noname_row(self):
      row = cmake_row(self.noname_var, 0)
      self.assertEqual(row.type, row.NOT_PARSABLE)

   def test_empty_line(self):
      row = cmake_row("", 0)
      self.assertEqual(row.type, row.EMPTY)

   def test_notype_row(self):
      row = cmake_row(self.notype_var, 0)
      self.assertEqual(row.type, row.NOT_PARSABLE)
