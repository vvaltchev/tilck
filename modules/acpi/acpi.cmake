# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

CHECK_C_COMPILER_FLAG(
   -Wno-unused-but-set-variable
   FLAG_WNO_UNUSED_BUT_SET_VAR
)

set(MOD_${mod}_SOURCES_GLOB "")

list(
   APPEND MOD_${mod}_SOURCES_GLOB

   # ACPICA source files
   "${CMAKE_SOURCE_DIR}/modules/${mod}/acpica/debugger/*.c"
   "${CMAKE_SOURCE_DIR}/modules/${mod}/acpica/dispatcher/*.c"
   "${CMAKE_SOURCE_DIR}/modules/${mod}/acpica/events/*.c"
   "${CMAKE_SOURCE_DIR}/modules/${mod}/acpica/executer/*.c"
   "${CMAKE_SOURCE_DIR}/modules/${mod}/acpica/hardware/*.c"
   "${CMAKE_SOURCE_DIR}/modules/${mod}/acpica/namespace/*.c"
   "${CMAKE_SOURCE_DIR}/modules/${mod}/acpica/parser/*.c"
   "${CMAKE_SOURCE_DIR}/modules/${mod}/acpica/resources/*.c"
   "${CMAKE_SOURCE_DIR}/modules/${mod}/acpica/tables/*.c"
   "${CMAKE_SOURCE_DIR}/modules/${mod}/acpica/utilities/*.c"

   # Our source files (OS Services Layer)
   "${CMAKE_SOURCE_DIR}/modules/${mod}/*.c"
)

file(GLOB MOD_${mod}_SOURCES ${GLOB_CONF_DEP} ${MOD_${mod}_SOURCES_GLOB})

add_library(
   mod_${mod} STATIC EXCLUDE_FROM_ALL
   ${MOD_${mod}_SOURCES}
)

target_include_directories(

   mod_${mod} PUBLIC

   "${CMAKE_SOURCE_DIR}/include/3rd_party/acpi"
)

set(
   ACPICA_FLAGS_LIST

   -D__MOD_ACPICA__
   -DACPI_DEBUGGER=1

   -include tilck/common/basic_defs.h
   -include tilck/common/string_util.h

   -Wno-conversion
   -Wno-unused-variable
)

if (FLAG_WNO_UNUSED_BUT_SET_VAR)
   list(APPEND ACPICA_FLAGS_LIST -Wno-unused-but-set-variable)
endif()

JOIN("${ACPICA_FLAGS_LIST}" ${SPACE} ACPICA_FLAGS)
set(flags "${KERNEL_FLAGS} ${ACTUAL_KERNEL_ONLY_FLAGS} ${ACPICA_FLAGS}")

set_target_properties(

   mod_${mod}

   PROPERTIES
      COMPILE_FLAGS "${flags}"
)

unset(flags)
target_link_libraries(${TARGET_NAME} mod_${mod})
