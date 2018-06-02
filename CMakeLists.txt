cmake_minimum_required(VERSION 3.2)
project (exos C CXX ASM)

include(CheckCCompilerFlag)

set(CMAKE_VERBOSE_MAKEFILE off)

# Reset the default flags

set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_C_FLAGS_MINSIZEREL "")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "")
set(CMAKE_C_FLAGS_RELEASE "")

###########################################################
# Generic CMake Utilities

set(SPACE " ")

function(JOIN VALUES GLUE OUTPUT)
  string (REPLACE ";" "${GLUE}" _TMP_STR "${VALUES}")
  set (${OUTPUT} "${_TMP_STR}" PARENT_SCOPE)
endfunction()

function(PREPEND var prefix)
   set(listVar "")
   foreach(f ${ARGN})
      list(APPEND listVar "${prefix}${f}")
   endforeach(f)
   set(${var} "${listVar}" PARENT_SCOPE)
endfunction(PREPEND)

###########################################################
# Architecture setting. Only x86 is accepted by now.

set(ARCH "i386")

# Other general config variables.

set(USE_GCOV CACHE BOOL "Compile the tests for measuring coverage")

###########################################################

set(BUILD_SCRIPTS ${CMAKE_SOURCE_DIR}/scripts/build_scripts)
set(BUILD_APPS ${CMAKE_BINARY_DIR}/scripts/build_apps)
set(BUILD_SHARED_LIBRARIES off)
set(TCROOT ${CMAKE_SOURCE_DIR}/toolchain)

if (${ARCH} STREQUAL "i386")
   set(ARCH_FAMILY "generic_x86")
   set(GCC_TOOLCHAIN ${TCROOT}/x86_gcc_toolchain/bin)
else()
   message(FATAL_ERROR "Architecture '${ARCH}' not supported.")
endif()



set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "C compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER_ID}")


CHECK_C_COMPILER_FLAG(-fno-tree-loop-vectorize FLAG_NO_TREE_LOOP_VEC_SUPPORTED)
CHECK_C_COMPILER_FLAG(-fno-tree-vectorize FLAG_NO_TREE_VEC_SUPPORTED)
CHECK_C_COMPILER_FLAG(-fno-tree-slp-vectorize FLAG_NO_TREE_SLP_VEC_SUPPORTED)

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

   -Wall
   -Wextra
   -Werror
   -Wno-unused-function
   -Wno-unused-parameter
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
   if(CMAKE_C_COMPILER_VERSION VERSION_LESS "5.0.0")
      # See http://gcc.gnu.org/bugzilla/show_bug.cgi?id=53119
      list(APPEND WARN_FLAGS_LIST "-Wno-missing-braces")
      list(APPEND WARN_FLAGS_LIST "-Wno-missing-field-initializers")
   endif()
endif()

JOIN("${GENERAL_DEFS_LIST}" ${SPACE} GENERAL_DEFS)
JOIN("${OTHER_CFLAGS_LIST}" ${SPACE} OTHER_CFLAGS)
JOIN("${OPT_FLAGS_LIST}" ${SPACE} OPT_FLAGS)
JOIN("${WARN_FLAGS_LIST}" ${SPACE} WARN_FLAGS)

set(COMMON_FLAGS "${GENERAL_DEFS} ${OTHER_CFLAGS} ${OPT_FLAGS} ${WARN_FLAGS}")

################################################################################
# Bootloaders & build-scripts config

set(BL_ST2_DATA_SEG 0x2000)
set(EARLY_BOOT_SZ 4096)
set(STAGE3_ENTRY_OFF 4096)

include("gen_config.cmake")

################################################################################

set(BOOTLOADER_BIN_FILE ${CMAKE_BINARY_DIR}/bootloader/bootloader.bin)
set(EFI_x86_64_FILE ${CMAKE_BINARY_DIR}/bootloader/efi/BOOTX64.EFI)
set(EFI_ia32_FILE ${CMAKE_BINARY_DIR}/bootloader/efi/BOOTIA32.EFI)
set(KERNEL_FILE ${CMAKE_BINARY_DIR}/elf_kernel_stripped)
set(IMG_FILE ${CMAKE_BINARY_DIR}/exos.img)
set(GPT_IMG_FILE ${CMAKE_BINARY_DIR}/gpt_exos.img)

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
      ${BUILD_SCRIPTS}/create_empty_img_if_necessary ${IMG_FILE} 4096 #37296 # ~36 MB
   COMMAND
      dd status=none conv=notrunc if=${BOOTLOADER_BIN_FILE} of=${IMG_FILE} bs=1K
   COMMAND
      ${BUILD_SCRIPTS}/build_fatpart ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} fatpart
   COMMAND
      ${BUILD_APPS}/fat_get_used_bytes --truncate fatpart
   COMMAND
      dd status=none conv=notrunc if=${CMAKE_BINARY_DIR}/fatpart of=${IMG_FILE} seek=1024 bs=1K
   DEPENDS
      # We MUST depend on BOTH each wrapper-target AND its OUTPUT in order to
      # our image file to be re-built when the *.bin files are re-built.
      bootloader
      efi_bootloader
      kernel
      init
      shell
      fat_get_used_bytes
      ${BUILD_SCRIPTS}/build_fatpart
      ${BOOTLOADER_BIN_FILE}
      ${EFI_x86_64_FILE}
      ${EFI_ia32_FILE}
      ${KERNEL_FILE}
   COMMENT
      "Creating the bootable image file (MBR)"
   VERBATIM
)


add_custom_target(

    exos_image ALL

    DEPENDS
        ${IMG_FILE}
)

add_dependencies(gtests exos_image)

add_custom_command(

   OUTPUT
      ${GPT_IMG_FILE}
   COMMAND
      ${BUILD_SCRIPTS}/create_empty_img_if_necessary ${GPT_IMG_FILE} 37296 #36MB
   COMMAND
      parted ${GPT_IMG_FILE} -s -a minimal mklabel gpt
   COMMAND
      parted ${GPT_IMG_FILE} -s -a minimal mkpart EFI FAT32 2048s 73727s
   COMMAND
      parted ${GPT_IMG_FILE} -s -a minimal toggle 1 boot
   COMMAND
      dd status=none conv=notrunc if=${CMAKE_BINARY_DIR}/fatpart of=${GPT_IMG_FILE} seek=1024 bs=1K
   DEPENDS
      ${IMG_FILE}
   COMMENT
      "Creating the bootable image file (GPT)"
)

add_custom_target(

   gpt_image

   DEPENDS
      ${GPT_IMG_FILE}
)
