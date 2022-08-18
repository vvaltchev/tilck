from typing import Dict, Any, Type, Mapping

class cmake_var:
   "generic type of cmake_var, used when we don't need particular conversions"
   def __init__(self, value: str) -> None:
      self.value: Any = value

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
      self.value = self.NORMALIZED_BOOLS[value]

   def serialize(self) -> str:
      return "ON" if self.value else "OFF"

def build_cmake_var(type: str, cmake_var_str: str) -> cmake_var:
   "factory function that associates the correct type of variable"
   cmake_type_str_to_class: Mapping[str, Type[cmake_var]] = {
      "BOOL" : cmake_var_bool,
      "INTERNAL" : cmake_var,
      "FILEPATH": cmake_var,
      "STRING" : cmake_var,
   }
   class_obj = cmake_type_str_to_class[type]
   return class_obj(cmake_var_str)
