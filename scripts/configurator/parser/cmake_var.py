from collections import namedtuple
from typing import Dict, Any, Optional, Type, Mapping, List
import re
from sys import stderr


class metadata:
   """By convention, first row is considered a user comment
   and is not considered in the metadata parsing process.
   Raises a ValueError if a keyword definition is repeated twice, or if an
   invalid keyword is provided."""
   keyword_expr = re.compile(r"^([A-Z_]+) (.*)")

   def __init__(self, comment: List[str]) -> None:
      kw_tuple = namedtuple("kw_tuple", ["parser", "setter", "getter"])
      """ If we wanted to use typehinting on kw_tuple, we should derive a new
      class from namedtuple and set arguments directly from there"""
      self.depends_on: Optional[List[str]] = None
      self.group: Optional[str] = None
      self.realtype: Optional[str] = None
      self.kw = {
         "REALTYPE": kw_tuple(self.parse_realtype,
                              self.get_setter("realtype"),
                              self.get_getter("realtype")),
         "DEPENDS_ON":
            kw_tuple(self.parse_depends_on,
                     self.get_setter("depends_on"),
                     self.get_getter("depends_on")),
         "GROUP":
            kw_tuple(self.parse_group,
                     self.get_setter("group"),
                     self.get_getter("group")),
      }

      for i, line in enumerate(comment):
         match = self.keyword_expr.match(line)
         if not match:
            continue

         key = match.group(1)
         if i == 0:
            if key in self.kw:
               msg = """WARNING: First line uses a reserved keyword: {}.
               This will be ignored."""
               stderr.write(msg.format(key))
            continue

         elif key not in self.kw:
            msg = "{} is not a valid metadata keyword."
            raise ValueError(msg.format(key))

         elif self.kw[key].getter() is not None:
            msg = "Key {} has already a value associated to itself."
            raise ValueError(msg.format(key))

         setter = self.kw[key].setter
         parsed_value = self.kw[key].parser(match.group(2))
         setter(parsed_value)
         assert(self.kw[key].getter() is not None)

   def get_setter(self, var_name: str):

      def setter(val) -> None:
         nonlocal self
         self.__dict__[var_name] = val

      return setter

   def get_getter(self, var_name: str):

      def getter():
         nonlocal self
         return self.__dict__[var_name]

      return getter

   def parse_realtype(self, string: str) -> str:
      return string

   def parse_group(self, string: str) -> str:
      return string

   def parse_depends_on(self, string: str) -> List[str]:
      return [s.strip() for s in string.split(",")]

class cmake_var:
   """
   General class used when we don't need particular conversions or validation.
   When either more validation or special serialization is needed, this class
   can be used as base.
   """
   def __init__(self, value: str) -> None:
      self.value: Any = value
      self.metadata: Optional[metadata] = None

   def serialize(self) -> str:
      return self.value

class cmake_var_bool(cmake_var):

   NORMALIZED_BOOLS: Dict[str, bool] = {
      "ON" : True,
      "OFF": False,
      "1" : True,
      "0" : False,
      "TRUE": True,
      "FALSE": False,
   }

   def __init__(self, value: str) -> None:
      self.value: bool = self.NORMALIZED_BOOLS[value]
      self.metadata: Optional[metadata] = None

   def serialize(self) -> str:
      return "ON" if self.value else "OFF"

def build_cmake_var(type: str, cmake_var_str: str, comment: List[str]
                    ) -> cmake_var:
   """
   Factory function that associates the correct type of variable and handles
   metadata, provided by the row's associated comments.
   """
   cmake_type_str_to_class: Mapping[str, Type[cmake_var]] = {
      "BOOL" : cmake_var_bool,
      "INTERNAL" : cmake_var,
      "FILEPATH": cmake_var,
      "STRING" : cmake_var,
   }

   loaded_metadata = metadata(comment)
   if loaded_metadata.realtype is not None:
      type = loaded_metadata.realtype

   class_obj = cmake_type_str_to_class[type]
   obj = class_obj(cmake_var_str)
   obj.metadata = loaded_metadata
   return obj
