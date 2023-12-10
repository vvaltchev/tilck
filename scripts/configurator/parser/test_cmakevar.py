# SPDX-License-Identifier: BSD-2-Clause

from unittest import TestCase
from cmake_var import metadata

class test_custom_metadata(TestCase):

   generic_comment = \
"""
GROUP KRN
DEPENDS_ON A, B, C
REALTYPE INT
""".splitlines()

   invalid_keyword_comment = \
"""
FOO KRN
""".splitlines()

   def test_metadata_creation_correct(self):
      m = metadata(self.generic_comment)
      with self.subTest():
         self.assertEqual(m.group, "KRN")
      with self.subTest():
         self.assertEqual(m.depends_on, ["A", "B", "C"])
      with self.subTest():
         self.assertEqual(m.realtype, "INT")

   def test_metdata_creation_invalid_keyword(self):
      self.assertRaises(ValueError, metadata, self.invalid_keyword_comment)
