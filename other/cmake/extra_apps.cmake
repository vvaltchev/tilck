# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

# Each EXTRA_* is gated on the presence of its upstream toolchain
# package. Options show up in menuconfig only when the corresponding
# package has been installed (./scripts/build_toolchain -s <pkg>);
# CMake never declares the option otherwise, so mconf's menu stays
# clean for users who haven't opted into those tools.

if (EXISTS ${TCROOT_ARCH_DIR}/vim/${VER_VIM})
   tilck_option(EXTRA_VIM
      TYPE     BOOL
      CATEGORY "Userapps/Extra"
      DEFAULT  OFF
      HELP     "Include the real VIM"
   )
   message(STATUS "EXTRA_VIM: ${EXTRA_VIM}")
endif()

if (EXISTS ${TCROOT_ARCH_DIR}/tcc/${VER_TCC})
   tilck_option(EXTRA_TCC
      TYPE     BOOL
      CATEGORY "Userapps/Extra"
      DEFAULT  OFF
      HELP     "Include the TinyCC compiler"
   )
   message(STATUS "EXTRA_TCC: ${EXTRA_TCC}")
endif()

if (EXISTS ${TCROOT_ARCH_DIR}/fbdoom/${VER_FBDOOM} AND
    EXISTS ${TCROOT_ARCH_DIR}/freedoom/${VER_FREEDOOM})
   tilck_option(EXTRA_FBDOOM
      TYPE     BOOL
      CATEGORY "Userapps/Extra"
      DEFAULT  OFF
      HELP     "Include fbDOOM"
   )
   message(STATUS "EXTRA_FBDOOM: ${EXTRA_FBDOOM}")
endif()

if (EXISTS ${TCROOT_ARCH_DIR}/micropython/${VER_MICROPYTHON})
   tilck_option(EXTRA_MICROPYTHON
      TYPE     BOOL
      CATEGORY "Userapps/Extra"
      DEFAULT  OFF
      HELP     "Include MicroPython"
   )
   message(STATUS "EXTRA_MICROPYTHON: ${EXTRA_MICROPYTHON}")
endif()

if (EXISTS ${TCROOT_ARCH_DIR}/treecmd/${VER_TREECMD})
   tilck_option(EXTRA_TREE_CMD
      TYPE     BOOL
      CATEGORY "Userapps/Extra"
      DEFAULT  OFF
      HELP     "Include the tree(1) command"
   )
   message(STATUS "EXTRA_TREE_CMD: ${EXTRA_TREE_CMD}")
endif()

if (EXISTS ${TCROOT_ARCH_DIR}/lua/${VER_LUA})
   tilck_option(EXTRA_LUA
      TYPE     BOOL
      CATEGORY "Userapps/Extra"
      DEFAULT  OFF
      HELP     "Include LUA"
   )
   message(STATUS "EXTRA_LUA: ${EXTRA_LUA}")
endif()

if (EXISTS ${TCROOT}/noarch/tfblib/${VER_TFBLIB})
   tilck_option(EXTRA_TFBLIB
      TYPE     BOOL
      CATEGORY "Userapps/Extra"
      DEFAULT  OFF
      HELP     "Include tfblib apps"
   )
   message(STATUS "EXTRA_TFBLIB: ${EXTRA_TFBLIB}")
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

if (EXTRA_TFBLIB)
   set(EXTRA_TFBLIB_ENABLED "1")
endif()
