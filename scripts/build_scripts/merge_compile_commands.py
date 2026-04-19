#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause

"""
Merge compile_commands.json files from the root CMake project and its
ExternalProject sub-builds into a single unified compile_commands.json
so clangd (and other LSP tooling) sees a whole-project view.

Tilck's build splits into several ExternalProject sub-projects (kernel,
userapps, boot_legacy, efi_x86_64, efi_ia32, gtests), each producing its
own compile_commands.json when configured with CMAKE_EXPORT_COMPILE_COMMANDS.
The root project also produces one covering scripts/build_apps/.

The merged output lives in a dedicated subdirectory (build/compile_db/)
instead of overwriting the root build/compile_commands.json. CMake owns the
canonical per-project files; this script produces a separate tooling-only
view that CMake never touches. Clangd is pointed at the merged directory
via --compile-commands-dir=build/compile_db or an equivalent .clangd entry.

De-duplication: entries with the same (file, directory) pair are kept once;
later entries win on collision (sub-project entries tend to be more specific
than root entries for shared sources like common/).
"""

import json
import os
import sys

OUT_SUBDIR = "compile_db"


def load(path):
   try:
      with open(path) as f:
         return json.load(f)
   except (FileNotFoundError, json.JSONDecodeError):
      return []


def main():
   if len(sys.argv) != 2:
      sys.exit(f"usage: {sys.argv[0]} <build_dir>")

   build_dir = os.path.abspath(sys.argv[1])
   out_dir = os.path.join(build_dir, OUT_SUBDIR)
   out_path = os.path.join(out_dir, "compile_commands.json")

   os.makedirs(out_dir, exist_ok=True)

   input_paths = []
   for dirpath, _, filenames in os.walk(build_dir):
      if "compile_commands.json" in filenames:
         p = os.path.join(dirpath, "compile_commands.json")
         if p != out_path:
            input_paths.append(p)

   input_paths.sort()

   merged = {}
   for sp in input_paths:
      for entry in load(sp):
         merged[(entry["file"], entry["directory"])] = entry

   tmp = out_path + ".tmp"
   with open(tmp, "w") as f:
      json.dump(list(merged.values()), f, indent=2)
   os.replace(tmp, out_path)

   print(f"Merged {len(input_paths)} compile_commands.json "
         f"({len(merged)} entries) -> {out_path}")


if __name__ == "__main__":
   main()
