# SPDX-License-Identifier: BSD-2-Clause
#
# From text to the keys that type it, by QEMU's QKeyCode names: the
# names `send-key` takes over QMP and `sendkey` takes in the HMP.
#
# One key press is a list of names: [name] for a plain key,
# [modifier, key] for a chord, pressed in order and released in
# reverse.

# The printable ASCII characters that are not a letter or a digit.
QEMU_SPECIAL_KEYS = {
   " "  : ["spc"],
   "!"  : ["shift", "1"],
   '"'  : ["shift", "apostrophe"],
   "#"  : ["shift", "3"],
   "$"  : ["shift", "4"],
   "%"  : ["shift", "5"],
   "&"  : ["shift", "7"],
   "'"  : ["apostrophe"],
   "("  : ["shift", "9"],
   ")"  : ["shift", "0"],
   "*"  : ["shift", "8"],
   "+"  : ["shift", "equal"],
   ","  : ["comma"],
   "-"  : ["minus"],
   "."  : ["dot"],
   "/"  : ["slash"],
   ":"  : ["shift", "semicolon"],
   ";"  : ["semicolon"],
   "<"  : ["shift", "comma"],
   "="  : ["equal"],
   ">"  : ["shift", "dot"],
   "?"  : ["shift", "slash"],
   "@"  : ["shift", "2"],
   "["  : ["bracket_left"],
   "\\" : ["backslash"],
   "]"  : ["bracket_right"],
   "^"  : ["shift", "6"],
   "_"  : ["shift", "minus"],
   "`"  : ["grave_accent"],
   "{"  : ["shift", "bracket_left"],
   "|"  : ["shift", "backslash"],
   "}"  : ["shift", "bracket_right"],
   "~"  : ["shift", "grave_accent"],
}

def key_codes(ch):
   """
   The key press that types the ASCII character `ch`, or None when no
   key does (control characters).
   """

   if "a" <= ch <= "z" or "0" <= ch <= "9":
      return [ch]

   if "A" <= ch <= "Z":
      return ["shift", ch.lower()]

   return QEMU_SPECIAL_KEYS.get(ch)

def keys_for_string(s):
   """
   The key presses that type `s`, in order. `{name}` is a key by its
   QKeyCode name (`{ret}`, `{esc}`, `{down}`), `{mod-name}` a chord
   (`{alt-f2}`, `{ctrl-t}`); QKeyCode names never contain a dash.
   Characters no key types are skipped.
   """

   keys = []
   i = 0

   while i < len(s):

      if s[i] == "{":

         j = s.find("}", i)

         if j < 0:
            raise ValueError("Unterminated {{ in {!r}".format(s))

         keys.append(s[i+1:j].split("-"))
         i = j + 1

      else:

         codes = key_codes(s[i])

         if codes:
            keys.append(codes)

         i += 1

   return keys
