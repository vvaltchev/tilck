# SPDX-License-Identifier: BSD-2-Clause

import os
import platform

import clang.cindex

# macOS: dyld doesn't search Xcode CLT / Homebrew LLVM, so the cindex
# bindings can't auto-locate libclang.dylib. Probe known install paths
# and point Config at the first hit. Linux/FreeBSD autoload via
# DT_NEEDED / ld.so.
if platform.system() == 'Darwin':
   for _path in (
      '/Library/Developer/CommandLineTools/usr/lib/libclang.dylib',
      '/opt/homebrew/opt/llvm/lib/libclang.dylib',
      '/usr/local/opt/llvm/lib/libclang.dylib',
   ):
      if os.path.exists(_path):
         clang.cindex.Config.set_library_file(_path)
         break
