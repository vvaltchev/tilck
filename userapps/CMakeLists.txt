# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

######################## COMMON CODE & DEFINITIONS #############################

set_cross_compiler_userapps()
add_definitions(-DUSERMODE_APP)

# Force static building for all the user applications
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${USERAPPS_CFLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${USERAPPS_CFLAGS}")

add_custom_target(userapps)
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${TCROOT}/${ARCH}/zlib/install/include)

# [build utils]

   macro(register_usermode_app)
      list(APPEND APPS_BIN_FILES "${CMAKE_BINARY_DIR}/userapps/${ARGV0}")
      add_dependencies(userapps ${ARGV0})
   endmacro()

   macro(add_usermode_app)

      if (NOT "${ARGV0}" STREQUAL "init")
         set(USERAPPS_${ARGV0} ON CACHE BOOL "Include `${ARGV0}` in fatpart")
      else()
         set(USERAPPS_init ON)
      endif()

      if (USERAPPS_${ARGV0})

         file(
            GLOB
            SOURCES
            "${ARGV1}${ARGV0}*.c"
         )

         add_executable(${ARGV0} ${SOURCES})
         register_usermode_app(${ARGV0})
      endif()

   endmacro()

# [/build utils]

################################ TARGETS #######################################

# [simple apps]
   add_usermode_app(init)
   add_usermode_app(termtest)
   add_usermode_app(fbtest)
   add_usermode_app(dp)
# [/simple apps]

# [filedump]
   file(
      GLOB
      FILEDUMP_SOURCES

      "filedump.c"
      "${CMAKE_SOURCE_DIR}/common/3rd_party/crc32.c"
   )

   set(USERAPPS_filedump OFF CACHE BOOL "Include `filedump` in fatpart")

   if (USERAPPS_filedump)
      add_executable(filedump ${FILEDUMP_SOURCES})
      register_usermode_app(filedump)
   endif()
# [/filedump]

# [devshell]
   file(
      GLOB
      DEVSHELL_SOURCES

      "devshell/*.c"
      "${CMAKE_SOURCE_DIR}/tests/system/*.c"
      "${CMAKE_SOURCE_DIR}/common/3rd_party/base64.c"
   )

   set(USERAPPS_devshell ON CACHE BOOL "Include `devshell` in fatpart")

   if (USERAPPS_devshell)
      add_executable(devshell ${DEVSHELL_SOURCES})
      set(DEVSHELL_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/devshell)
      target_include_directories(devshell PUBLIC ${DEVSHELL_SRC_DIR})
      target_link_libraries(devshell ${TCROOT}/${ARCH}/zlib/install/lib/libz.a)
      register_usermode_app(devshell)
   endif()
# [/devshell]

# [ncurses test app]
   if (EXISTS ${NCURSES_INST}/lib/libncurses_g.a)

      set(USERAPPS_ncapp OFF CACHE BOOL "Include `ncapp` in fatpart")

      if (USERAPPS_ncapp)
         add_usermode_app(ncapp)
         target_include_directories(ncapp PUBLIC ${NCURSES_INST}/include)
         target_link_libraries(ncapp ${NCURSES_INST}/lib/libncurses_g.a)
      endif()
   endif()
# [/ncurses test app]

# [extra]
# This macro makes the integration with extra apps much easier.
# It's enough for the external app to just add a call to tilck_add_extra_app()
# with the full-path of a binary in order to that deliverable to become a file
# dependency of Tilck's main target (the image) and get that deliverable copied
# in /usr/bin (in Tilck's fatpart).
# Example: https://github.com/vvaltchev/tfblib

   set(USERAPPS_extra ON CACHE BOOL "Build all apps in userapps/extra")

   macro(tilck_add_extra_app)
      set(EXTRA_APPS_LIST_copy ${EXTRA_APPS_LIST})
      list(APPEND EXTRA_APPS_LIST_copy ${ARGV0})
      set(EXTRA_APPS_LIST ${EXTRA_APPS_LIST_copy})
      set(EXTRA_APPS_LIST ${EXTRA_APPS_LIST_copy} PARENT_SCOPE)
      file(RELATIVE_PATH relPath "${CMAKE_BINARY_DIR}" "${ARGV0}")
      message(STATUS "Adding EXTRA APP: ${relPath}")
   endmacro()

   if (USERAPPS_extra)
      add_subdirectory(extra)
   endif()

   set(USERAPPS_FILES_LIST ${APPS_BIN_FILES} PARENT_SCOPE)
   set(EXTRA_APPS_LIST ${EXTRA_APPS_LIST} PARENT_SCOPE)

# [/extra]


# [busybox]

   if (EXISTS ${BUSYBOX_CONFIG})
      execute_process(
         COMMAND ${CMAKE_COMMAND}
         -E compare_files
            ${BUSYBOX_CONFIG}
            ${CMAKE_SOURCE_DIR}/other/busybox.config
         RESULT_VARIABLE result
      )

      if (result EQUAL 1)

         set(msg1 "")
         string(CONCAT msg1 "The busybox build in the toolchain has a "
                            "different config than the one in the source. "
                            "This might have undesired effects. ")

         if ("$ENV{CIRCLECI}" STREQUAL "" AND
             "$ENV{TRAVIS}" STREQUAL "" AND
             "$ENV{CI}" STREQUAL "")

            set(msg2 "")
            string(CONCAT msg2 "How to fix it: \n"
                               "   1) rm -rf ${TCROOT_REL}/${ARCH}/busybox\n"
                               "   2) ${BTC_SCRIPT_REL} -s build_busybox")

            message(FATAL_ERROR "\n${msg1}\n${msg2}")

         else()

            message(WARNING "\n${msg1}\n${msg2}")

         endif()

      elseif (result EQUAL 0)

         # Nothing to do, everything is fine :-)

      else()
         message(FATAL_ERROR "Error while comparing the busybox config files.")
      endif()

   else()

      message(WARNING "\nNo busybox in toolchain.")

   endif()

# [/busybox]
