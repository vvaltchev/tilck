
/*
 * This is a TEMPLATE. The actual "generated_config.h" is generated my CMake
 * and put in project's build directory.
 */


#pragma once

#define PROJ_BUILD_DIR         "@CMAKE_BINARY_DIR@"
#define BUILDTYPE_STR          "@CMAKE_BUILD_TYPE@"

#define BL_ST2_DATA_SEG        (@BL_ST2_DATA_SEG@)
#define BL_BASE_ADDR           (@BL_BASE_ADDR@)
#define CMAKE_KERNEL_BASE_VA   (@CMAKE_KERNEL_BASE_VA@)
#define KERNEL_PADDR           (@CMAKE_KERNEL_PADDR@)

#define KERNEL_FILE_PATH       "/@KERNEL_FATPART_PATH@"
#define KERNEL_FILE_PATH_EFI   "\\@KERNEL_FATPART_PATH_EFI@"
