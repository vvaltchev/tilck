# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

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

if (EXISTS ${TCROOT}/${ARCH}/tree_cmd)
   set(EXTRA_TREE_CMD OFF CACHE BOOL "Load the tree command Tilck")
   message(STATUS "EXTRA_TREE_CMD: ${EXTRA_TREE_CMD}")
endif()

if (EXISTS ${TCROOT}/${ARCH}/lua)
   set(EXTRA_LUA OFF CACHE BOOL "Load LUA in Tilck")
   message(STATUS "EXTRA_LUA: ${EXTRA_LUA}")
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
   set(EXTRA_MICROPYTHON_ENABLED "1")
endif()

if (EXTRA_TREE_CMD)
   set(EXTRA_TREE_CMD_ENABLED "1")
endif()

if (EXTRA_LUA)
   set(EXTRA_LUA_ENABLED "1")
endif()
