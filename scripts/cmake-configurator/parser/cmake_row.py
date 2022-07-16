import re
from typing import Match, Optional

class cmake_row:
   """
   simple utility class that abstracts away some details from the parsing
   process from a single CMakeCache.txt row. This is just an intermediate
   layer between cmake_variable and cmake_row, since we still need to keep
   either file_lines or an array of cmake_row. Among these 2 it make more sense
   to just keep file_lines.
   """
   EMPTY: str = "EMPTY"
   SLASH_COMMENT :str = "SLASH_COMMENT"
   HASHTAG_COMMENT: str = "HASHTAG_COMMENT"
   VARIABLE: str = "VARIABLE"
   NOT_PARSABLE: str = "NOT_PARSABLE"
   regex_expression: str =  r"^([A-Za-z_0-9]+?):([A-Za-z]+?)=(.*)$"

   BOOL_MAPPINGS = {
      "ON" : "ON",
      "OFF": "OFF",
      "0" : "OFF",
      "1" : "ON",
      "TRUE": "ON",
      "FALSE": "OFF",
   }

   def __init__(self, raw_row: str, row_number: int):
      """
      strips variable and assigns its correct type by matching the regex
      and verifying if it's a comment.
      """
      row  = raw_row.strip()
      matches: Optional[Match[str]]  = re.match(self.regex_expression, row)
      self.type: str =  self.NOT_PARSABLE
      self.val: str = ""
      self.name: str = ""
      self.var_type: str = ""
      self.row_number: int = row_number
      if not len(row):
         self.type = self.EMPTY
         return

      if self.is_comment(row):
         if self.is_slash_comment(row):
            self.type = self.SLASH_COMMENT
            self.val = row[2:]
            return

         self.type = self.HASHTAG_COMMENT
         self.val = row[1:]
         return

      elif matches: # this guarantees that the regex has exactly 3 matches
         groups = matches.groups()
         self.name = groups[0]
         self.type = self.VARIABLE
         self.val = groups[2]
         self.var_type = groups[1]
         if self.var_type == "BOOL":
            self.val = self.BOOL_MAPPINGS[self.val]

   def is_comment(self, row: str) -> bool:
      "checks if string is either // comment or #comment"
      return self.is_slash_comment(row) or self.is_hashtag_comment(row)

   def is_slash_comment(self, row: str) -> bool:
      return row[:2] == "//"

   def is_hashtag_comment(self, row: str) -> bool:
      return row[0] == "#"

   def serialize(self) -> str:
      """
      returns serialized version of variable. Expects to be called only on
      variable types.
      """
      return self.name + ":" + self.var_type + "=" + self.val

   def normalize_bool(self, val: str):
      return self.BOOL_MAPPINGS[val]
