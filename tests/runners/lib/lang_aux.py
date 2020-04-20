# SPDX-License-Identifier: BSD-2-Clause
import sys

class ConstError(TypeError):

   def __init__(self, name):
      super(ConstError, self).__init__()
      self.name = name

   def __str__(self):
      return "Cannot re-bind '{}'".format(self.name)

class Const:

   def __init__(self, val):
      self.val = val

   def __setattr__(self, name, val):

      if name in self.__dict__:
         raise ConstError(name)

      self.__dict__[name] = val

class ConstModule:

   def __init__(self, mod):

      for pair in sys.modules[mod].__dict__.items():
         if type(pair[1]) is Const:
            self.__dict__[pair[0]] = pair[1].val

   def __setattr__(self, name, value):

      if name in self.__dict__:
         raise ConstError(name)

      self.__dict__[name] = value
