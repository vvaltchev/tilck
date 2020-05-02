# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

set(GENERAL_DEFS_LIST "")

if (CMAKE_BUILD_TYPE STREQUAL "Release")

   message(STATUS "Preparing a RELEASE build...")
   list(APPEND GENERAL_DEFS_LIST "-DNDEBUG -DRELEASE")
   set(OPT_FLAGS_LIST -O3)

else()

   message(STATUS "Preparing a DEBUG build...")
   list(APPEND GENERAL_DEFS_LIST "-DDEBUG")
   set(OPT_FLAGS_LIST -O0 -fno-inline-functions)

endif()

if (TEST_GCOV OR KERNEL_GCOV)
   if (NOT EXISTS ${TCROOT}/noarch/lcov)
      show_missing_lcov_error()
   endif()
endif()

if (TEST_GCOV)
   set(GCOV_COMPILE_FLAGS "-fprofile-arcs -ftest-coverage")
   set(GCOV_LINK_FLAGS "-fprofile-arcs -lgcov")
endif()

set(
   OTHER_CFLAGS_LIST

   -ggdb
   -fno-strict-aliasing
)

set(
   WARN_FLAGS_LIST

   -Wall
   -Wextra
   -Werror
   -Wshadow
   -Wno-unused-function
   -Wno-unused-parameter
)

if (CMAKE_C_COMPILER_ID STREQUAL "GNU")

   if (WCONV)
      show_wconv_warning()
   endif()

   if (CMAKE_C_COMPILER_VERSION VERSION_LESS "5.0.0")
      # See http://gcc.gnu.org/bugzilla/show_bug.cgi?id=53119
      list(APPEND WARN_FLAGS_LIST "-Wno-missing-braces")
      list(APPEND WARN_FLAGS_LIST "-Wno-missing-field-initializers")
   endif()
endif()

if (CMAKE_C_COMPILER_ID STREQUAL "Clang")

   list(APPEND WARN_FLAGS_LIST "-Wno-missing-braces")

   if (USE_SYSCC)
      show_clang_and_syscc_error()
   endif()

   if (NOT KERNEL_SYSCC)
      if (WCONV)
         show_wconv_warning()
      endif()
   endif()

endif()


JOIN("${GENERAL_DEFS_LIST}" ${SPACE} GENERAL_DEFS)
JOIN("${OTHER_CFLAGS_LIST}" ${SPACE} OTHER_CFLAGS)
JOIN("${OPT_FLAGS_LIST}"    ${SPACE} OPT_FLAGS)
JOIN("${WARN_FLAGS_LIST}"   ${SPACE} WARN_FLAGS)

set(COMMON_FLAGS "${GENERAL_DEFS} ${OTHER_CFLAGS} ${OPT_FLAGS} ${WARN_FLAGS}")

# Kernel flags

set(
   GENERAL_KERNEL_FLAGS_LIST

   -D__TILCK_KERNEL__

   -mno-red-zone
   -ffreestanding
   -fno-builtin
   -fno-omit-frame-pointer
   -fwrapv
   -Wno-unused-label
   -Wvla
)

if (WCONV)
   list(APPEND GENERAL_KERNEL_FLAGS_LIST "-Wconversion")
endif()

JOIN("${GENERAL_KERNEL_FLAGS_LIST}" ${SPACE} GENERAL_KERNEL_FLAGS)
set(KERNEL_FLAGS "${COMMON_FLAGS} ${GENERAL_KERNEL_FLAGS}")

set(
   KERNEL_NO_ARCH_FLAGS_LIST

   -DKERNEL_TEST
   -fPIC
   ${KERNEL_FLAGS}
   ${GCOV_COMPILE_FLAGS}
)

JOIN("${KERNEL_NO_ARCH_FLAGS_LIST}" ${SPACE} KERNEL_NO_ARCH_FLAGS)

set(
   KERNEL_CXX_FLAGS_LIST

   -fno-use-cxa-atexit
   -fno-rtti
)

JOIN("${KERNEL_CXX_FLAGS_LIST}" ${SPACE} KERNEL_CXX_FLAGS)
