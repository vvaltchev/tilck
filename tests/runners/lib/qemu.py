# SPDX-License-Identifier: BSD-2-Clause

QEMU_SPECIAL_KEYS = {
   " "  : "spc",
   "!"  : "shift-1",
   '"'  : "shift-apostrophe",
   "#"  : "shift-3",
   "$"  : "shift-4",
   "%"  : "shift-5",
   "&"  : "shift-7",
   "'"  : "apostrophe",
   "("  : "shift-9",
   ")"  : "shift-0",
   "*"  : "shift-8",
   "+"  : "shift-equal",
   ","  : "comma",
   "-"  : "minus",
   "."  : "dot",
   "/"  : "slash",
   ":"  : "shift-semicolon",
   ";"  : "semicolon",
   "<"  : "shift-comma",
   "="  : "equal",
   ">"  : "shift-dot",
   "?"  : "shift-slash",
   "@"  : "shift-2",
   "["  : "bracket_left",
   "\\" : "backslash",
   "]"  : "bracket_right",
   "^"  : "shift-6",
   "_"  : "shift-minus",
   "`"  : "grave_accent",
   "{"  : "shift-bracket_left",
   "|"  : "shift-backslash",
   "}"  : "shift-bracket_right",
   "~"  : "shift-grave_accent"
}

KEYS_MAP = {
   chr(x):
      chr(x)
         if x in range(ord('a'), ord('z')+1) or x in range(ord('0'),ord('9')+1)
         else
         'shift-' + chr(x).lower()
            if x in range(ord('A'), ord('Z')+1)
            else
            QEMU_SPECIAL_KEYS[chr(x)]
               if chr(x) in QEMU_SPECIAL_KEYS
               else None

   for x in range(27, 128)
}
