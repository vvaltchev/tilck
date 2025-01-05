# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

CHECK_C_COMPILER_FLAG(
   -Wno-unused-but-set-variable
   FLAG_WNO_UNUSED_BUT_SET_VAR
)

unset(acpica_sources_glob)
unset(osl_sources_glob)

set(ACPICA_ROOT ${TCROOT}/noarch/acpica)
set(ACPICA_SOURCE_ROOT ${ACPICA_ROOT}/source)
set(ACPICA_COMPS ${ACPICA_SOURCE_ROOT}/components)
set(ACPICA_INCLUDE ${ACPICA_SOURCE_ROOT}/include)

list(
   APPEND acpica_sources_glob

   # ACPICA source files
   "${ACPICA_COMPS}/dispatcher/*.c"
   "${ACPICA_COMPS}/events/*.c"
   "${ACPICA_COMPS}/executer/*.c"
   "${ACPICA_COMPS}/hardware/*.c"
   "${ACPICA_COMPS}/namespace/*.c"
   "${ACPICA_COMPS}/parser/*.c"
   "${ACPICA_COMPS}/resources/*.c"
   "${ACPICA_COMPS}/tables/*.c"
   "${ACPICA_COMPS}/utilities/*.c"
)

# Note: for the moment there's no support in Tilck for ACPI's debugger because
# some debugger functions are not implemented yet in the OS services layer.
# Still, it makes sense to be prepared and check for ACPI_DEBUGGER_ENABLED.
if (ACPI_DEBUGGER_ENABLED)
   list(
      APPEND acpica_sources_glob
      "${ACPICA_COMPS}/debugger/*.c"
   )
endif()

list(
   APPEND osl_sources_glob

   # Our source files (OS Services Layer)
   "${CMAKE_SOURCE_DIR}/modules/${mod}/*.c"
)

file(GLOB acpica_sources ${GLOB_CONF_DEP} ${acpica_sources_glob})
file(GLOB osl_sources ${GLOB_CONF_DEP} ${osl_sources_glob})

add_library(
   acpica STATIC EXCLUDE_FROM_ALL
   ${acpica_sources}
)

add_library(
   acpi_osl STATIC EXCLUDE_FROM_ALL
   ${osl_sources}
)

target_include_directories(
   acpica PUBLIC
   "${ACPICA_INCLUDE}"
)

target_include_directories(
   acpi_osl PUBLIC
   "${ACPICA_ROOT}"      # Used to include headers like <acpi/xyz.h>
   "${ACPICA_INCLUDE}"
)

set(
   COMMON_ACPI_FLAGS_LIST

   -DACPI_DEBUG_OUTPUT=1
   -Wno-conversion
   -Wno-error
)

if (ACPI_DEBUGGER_ENABLED)
   list(
      APPEND COMMON_ACPI_FLAGS_LIST
      -DACPI_DEBUGGER=1
   )
endif()

set(
   ACPICA_FLAGS_LIST

   -include tilck/common/basic_defs.h
   -include tilck/common/string_util.h

   -Wno-unused-variable
   -fno-sanitize=all
)

if (FLAG_WNO_UNUSED_BUT_SET_VAR)
   list(APPEND ACPICA_FLAGS_LIST -Wno-unused-but-set-variable)
endif()

JOIN("${COMMON_ACPI_FLAGS_LIST}" ${SPACE} COMMON_ACPI_FLAGS)
JOIN("${ACPICA_FLAGS_LIST}" ${SPACE} ACPICA_FLAGS)

set(krn_flags "${KERNEL_FLAGS} ${ACTUAL_KERNEL_ONLY_FLAGS}")
set(flags "${krn_flags} ${COMMON_ACPI_FLAGS}")

set_target_properties(

   acpica

   PROPERTIES
      COMPILE_FLAGS "-D__MOD_ACPICA__ ${flags} ${ACPICA_FLAGS}"
)

set_target_properties(

   acpi_osl

   PROPERTIES
      COMPILE_FLAGS "${flags} -D_COMPONENT=ACPI_OS_SERVICES"
)

unset(flags)
unset(krn_flags)
target_link_libraries(${TARGET_NAME} acpica)
target_link_libraries(${TARGET_NAME} acpi_osl)
