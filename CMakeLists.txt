cmake_minimum_required(VERSION 3.2)
project (exos C CXX ASM)

set(CMAKE_VERBOSE_MAKEFILE off)

###########################################################
# Generic CMake Utilities

set(SPACE " ")

function(JOIN VALUES GLUE OUTPUT)
  string (REPLACE ";" "${GLUE}" _TMP_STR "${VALUES}")
  set (${OUTPUT} "${_TMP_STR}" PARENT_SCOPE)
endfunction()

FUNCTION(PREPEND var prefix)
   SET(listVar "")
   FOREACH(f ${ARGN})
      LIST(APPEND listVar "${prefix}${f}")
   ENDFOREACH(f)
   SET(${var} "${listVar}" PARENT_SCOPE)
ENDFUNCTION(PREPEND)

###########################################################
# Architecture setting. Only x86 is accepted by now.

set(ARCH "i386")

# Other general config variables.

set(USE_GCOV CACHE BOOL "Compile the tests for measuring coverage")

###########################################################

if (${ARCH} STREQUAL "i386")
   set(ARCH_FAMILY "generic_x86")
else()
   message(FATAL_ERROR "Architecture '${ARCH}' not supported.")
endif()

set(BUILD_SCRIPTS ${CMAKE_SOURCE_DIR}/scripts/build_scripts)
set(BUILD_APPS ${CMAKE_BINARY_DIR}/scripts/build_apps)
set(BUILD_SHARED_LIBRARIES off)
set(TCROOT ${CMAKE_SOURCE_DIR}/toolchain)


set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "C compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER_ID}")

#message("CMAKE_ASM-ATT_COMPILE_OBJECT: ${CMAKE_ASM-ATT_COMPILE_OBJECT}")
#message("CMAKE_C_LINK_EXECUTABLE: ${CMAKE_C_LINK_EXECUTABLE}")
#message("CMAKE_C_COMPILE_OBJECT: ${CMAKE_C_COMPILE_OBJECT}")
#message("CMAKE_CXX_COMPILE_OBJECT: ${CMAKE_CXX_COMPILE_OBJECT}")

set(GENERAL_DEFS_LIST "")

if (CMAKE_BUILD_TYPE STREQUAL "Release")

   message(STATUS "Preparing a RELEASE build...")
   LIST(APPEND GENERAL_DEFS_LIST "-DNDEBUG")
   set(OPT_FLAGS_LIST -O2)

else()

   message(STATUS "Preparing a DEBUG build...")
   LIST(APPEND GENERAL_DEFS_LIST "-DDEBUG")
   set(OPT_FLAGS_LIST -O0 -fno-inline-functions)

endif()

set (GCOV_COMPILE_FLAGS "")
set (GCOV_LINK_FLAGS "")

if (${USE_GCOV})
   message(STATUS "Using GCOV for code coverage")
   set(GCOV_COMPILE_FLAGS "-fprofile-arcs -ftest-coverage")
   set(GCOV_LINK_FLAGS "-fprofile-arcs -lgcov")
endif()


set(
   OTHER_CFLAGS_LIST

   -g
)


set(
   WARN_FLAGS_LIST

   -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Werror
   -Wno-missing-braces # See http://gcc.gnu.org/bugzilla/show_bug.cgi?id=53119
)

JOIN("${GENERAL_DEFS_LIST}" ${SPACE} GENERAL_DEFS)
JOIN("${OTHER_CFLAGS_LIST}" ${SPACE} OTHER_CFLAGS)
JOIN("${OPT_FLAGS_LIST}" ${SPACE} OPT_FLAGS)
JOIN("${WARN_FLAGS_LIST}" ${SPACE} WARN_FLAGS)

set(COMMON_FLAGS "${GENERAL_DEFS} ${OTHER_CFLAGS} ${OPT_FLAGS} ${WARN_FLAGS}")

set(BOOTLOADER_BIN_FILE ${CMAKE_BINARY_DIR}/bootloader/bootloader.bin)
set(EFI_MAIN_FILE ${CMAKE_BINARY_DIR}/bootloader/efi/BOOTX64.EFI)
set(KERNEL_FILE ${CMAKE_BINARY_DIR}/elf_kernel_stripped)
set(IMG_FILE ${CMAKE_BINARY_DIR}/exos.img)

################################################################################
# Include sub-directories

add_subdirectory(bootloader)
add_subdirectory(bootloader/efi)
add_subdirectory(kernel)
add_subdirectory(usermode_apps)
add_subdirectory(unittests)
add_subdirectory(scripts/build_apps)

################################################################################

add_custom_command(

   OUTPUT
      ${IMG_FILE}
   COMMAND
      ${BUILD_SCRIPTS}/create_empty_img_if_necessary ${IMG_FILE} 37296 # ~36 MB
   COMMAND
      dd status=none conv=notrunc if=${BOOTLOADER_BIN_FILE} of=${IMG_FILE} bs=1K
   COMMAND
      ${BUILD_SCRIPTS}/build_fatpart ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} fatpart
   COMMAND
      dd status=none conv=notrunc if=${CMAKE_BINARY_DIR}/fatpart of=${IMG_FILE} seek=1024 bs=1K
   DEPENDS
      # We MUST depend on BOTH each wrapper-target AND its OUTPUT in order to
      # our image file to be re-built when the *.bin files are re-built.
      elfhack
      bootloader
      efi_bootloader
      kernel
      init
      shell
      ${BUILD_SCRIPTS}/build_fatpart
      ${BOOTLOADER_BIN_FILE}
      ${EFI_MAIN_FILE}
      ${KERNEL_FILE}
   COMMENT
      "Creating the bootable image file"
   VERBATIM
)


add_custom_target(

    exos_image ALL

    DEPENDS
        ${IMG_FILE}
)

add_dependencies(gtests exos_image)
