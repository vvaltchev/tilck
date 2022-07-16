from cmake_row import cmake_row

class cmake_variable:
   """
   Deals with I/O and abstracts cmake_row.
   """

   def __repr__(self) -> str:
      "used mainly for debugging, returns serialized variable and comment"
      return "{}  {}\n".format(self.row.serialize(), self.comment)

   def __init__(self, row: cmake_row, comment: str) -> None:
      self.comment : str = comment
      self.row: cmake_row = row

   def is_valid(self, user_input: str) -> bool:
      """
      Checks if the user input is valid.
      TODO: this might need to be extened in the future.
      """
      if self.row.var_type == "BOOL":
         return user_input.upper() in self.row.BOOL_MAPPINGS
      elif self.row.var_type == "INTERNAL":
         return False
      return True
