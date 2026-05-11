# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

project(userapps LANGUAGES C CXX ASM)

include(${TILCK_CONFIG_FILE})
set(GLOB_CONF_DEP CONFIGURE_DEPENDS)

######################## COMMON CODE & DEFINITIONS #############################

include(${PROJ_ROOT}/other/cmake/utils.cmake)
include(${PROJ_ROOT}/other/cmake/errors.cmake)
include(${PROJ_ROOT}/other/cmake/compiler_flags.cmake)

add_definitions(-DUSERMODE_APP)

# Force static building for all the user applications
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${USERAPPS_CFLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${USERAPPS_CFLAGS}")

add_custom_target(userapps)
include_directories(${BUILD_DIR})                 # For tilck_gen_headers/
include_directories(${PROJ_ROOT}/include)
include_directories(${PROJ_ROOT}/include/system_headers)
include_directories(${ZLIB_DIR}/install/include)

# [build utils]

   macro(register_usermode_app)
      list(APPEND APPS_BIN_FILES "${CMAKE_BINARY_DIR}/${ARGV0}")
      add_dependencies(userapps ${ARGV0})
   endmacro()

   macro(add_usermode_app)
      if (USERAPPS_${ARGV0})
         file(GLOB SOURCES ${GLOB_CONF_DEP} "${ARGV1}${ARGV0}*.c")
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
   add_usermode_app(play)
# [/simple apps]

# [dp]
   file(
      GLOB
      DP_SOURCES
      ${GLOB_CONF_DEP}

      "dp/*.c"
   )

   if (USERAPPS_dp)
      add_executable(dp ${DP_SOURCES})
      target_include_directories(dp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dp)
      register_usermode_app(dp)
   endif()
# [/dp]

# [tracer]
   #
   # The `tracer` binary shares the dp termutil/input/screen
   # infrastructure (so the trace screen looks and behaves like a dp
   # panel — banner + key dispatch + line editor + alt buffer), but
   # the trace-rendering code (~1.5 KLOC of tr_*.c) ships in its own
   # binary so dp itself stays slim and the tracer can run in builds
   # where MOD_debugpanel is off.
   #
   file(
      GLOB
      TRACER_SOURCES
      ${GLOB_CONF_DEP}

      "tracer/*.c"
      "dp/dp_input.c"
      "dp/dp_screen.c"
      "dp/termutil.c"
      "dp/task_dump.c"
   )

   if (USERAPPS_dp)
      add_executable(tracer ${TRACER_SOURCES})
      target_include_directories(
         tracer PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/tracer
            ${CMAKE_CURRENT_SOURCE_DIR}/dp
      )
      register_usermode_app(tracer)
   endif()
# [/tracer]

# [filedump]
   file(
      GLOB
      FILEDUMP_SOURCES

      "filedump.c"
      "${PROJ_ROOT}/common/3rd_party/crc32.c"
   )

   if (USERAPPS_filedump)
      add_executable(filedump ${FILEDUMP_SOURCES})
      register_usermode_app(filedump)
   endif()
# [/filedump]

# [devshell]
   file(
      GLOB
      DEVSHELL_SOURCES
      ${GLOB_CONF_DEP}

      "devshell/*.c"
      "devshell/${ARCH}.S"
      "${PROJ_ROOT}/tests/system/*.c"
      "${PROJ_ROOT}/common/3rd_party/base64.c"
   )

   if (USERAPPS_devshell)
      add_executable(devshell ${DEVSHELL_SOURCES})
      set(DEVSHELL_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/devshell)
      target_include_directories(devshell PUBLIC ${DEVSHELL_SRC_DIR})
      target_link_libraries(
         devshell ${ZLIB_DIR}/install/lib/libz.a
      )
      register_usermode_app(devshell)
   endif()
# [/devshell]

# [ncurses test app]
   if (EXISTS ${NCURSES_DIR}/install/lib/libncurses_g.a)
      if (USERAPPS_ncapp)
         add_usermode_app(ncapp)
         target_include_directories(ncapp PUBLIC ${NCURSES_DIR/install}/include)
         target_link_libraries(ncapp ${NCURSES_DIR}/install/lib/libncurses_g.a)
      endif()
   endif()
# [/ncurses test app]

# Write the apps list for build_fatpart
file(
   WRITE
   "${CMAKE_BINARY_DIR}/list.txt"
   "${APPS_BIN_FILES}"
)

# [busybox]

   if (USERAPPS_busybox)

      if (EXISTS ${BUSYBOX_CONFIG})
         execute_process(
            COMMAND ${CMAKE_COMMAND}
            -E compare_files
               ${BUSYBOX_CONFIG}
               ${PROJ_ROOT}/other/busybox.config
            RESULT_VARIABLE result
         )

         if (result EQUAL 1)

            set(msg1 "")
            string(CONCAT msg1 "The busybox build in the toolchain has a "
                              "different config than the one in the source. "
                              "This might have undesired effects. "
                              "Please update your busybox build.")

            if ("$ENV{CI}" STREQUAL "")

               set(msg2 "")
               string(CONCAT msg2 "How to do that: \n"
                                 "   1) ${BTC_SCRIPT_REL} -u busybox\n"
                                 "   2) ${BTC_SCRIPT_REL} -s busybox")

               message(FATAL_ERROR "\n${msg1}\n${msg2}")

            else()

               message(WARNING "\n${msg1}")

            endif()

         elseif (result EQUAL 0)

            # Nothing to do, everything is fine :-)

         else()
            message(FATAL_ERROR
                    "Error while comparing the busybox config files.")
         endif()

      else()

         message(WARNING "\nNo busybox in toolchain.")

      endif()
   endif(USERAPPS_busybox)

# [/busybox]

# Extra user apps (e.g. tfblib) -- each writes a manifest of deliverables
if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/extra)
   add_subdirectory(extra)
endif()
