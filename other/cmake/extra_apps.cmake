# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

if (EXISTS ${TCROOT}/${ARCH}/vim)
   set(EXTRA_VIM OFF CACHE BOOL "Load the real VIM in Tilck")
   message(STATUS "EXTRA_VIM: ${EXTRA_VIM}")
endif()

if (EXISTS ${TCROOT}/${ARCH}/tcc)
   set(EXTRA_TCC OFF CACHE BOOL "Load the TinyCC compiler in Tilck")
   message(STATUS "EXTRA_TCC: ${EXTRA_TCC}")
endif()

if (EXISTS ${TCROOT}/${ARCH}/fbDOOM)
   set(EXTRA_FBDOOM OFF CACHE BOOL "Load fbDOOM in Tilck")
   message(STATUS "EXTRA_FBDOOM: ${EXTRA_FBDOOM}")
endif()

if (EXISTS ${TCROOT}/${ARCH}/micropython)
   set(EXTRA_MICROPYTHON OFF CACHE BOOL "Load micropython in Tilck")
   message(STATUS "EXTRA_MICROPYTHON: ${EXTRA_MICROPYTHON}")
endif()

if (EXTRA_VIM)
   set(EXTRA_VIM_ENABLED "1")
endif()

if (EXTRA_TCC)
   set(msg "")
   string(CONCAT msg "The TinyCC compiler works on Tilck, but can be used "
                     "only for generatic static binaries, as the kernel does "
                     "not support dynamic linking, at the moment.")
   message(WARNING "${msg}")
   set(EXTRA_TCC_ENABLED "1")
endif()

if (EXTRA_FBDOOM)
   set(EXTRA_FBDOOM_ENABLED "1")
endif()

if (EXTRA_MICROPYTHON)
   set(msg "")
   string(CONCAT msg "Micropython works only partially in Tilck: it cannot "
                     "for example load a python file from the command line "
                     "because it uses libmusl's realpath() function which, "
                     "unfortunately, uses /proc/self/fd/<N>. Tilck does not "
                     "support /proc at all, for the moment. Therefore, "
                     "at the moment micropython works only in REPL mode, "
                     "which is good enough only as a proof-of-concept.")
   message(WARNING "${msg}")
   set(EXTRA_MICROPYTHON_ENABLED "1")
endif()
