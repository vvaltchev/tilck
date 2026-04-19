# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

set(SPACE " ")

function(JOIN VALUES GLUE OUTPUT)
  string(REPLACE ";" "${GLUE}" _TMP_STR "${VALUES}")
  set(${OUTPUT} "${_TMP_STR}" PARENT_SCOPE)
endfunction()

function(PREPEND var prefix)

   set(listVar "")

   foreach(f ${ARGN})
      list(APPEND listVar "${prefix}${f}")
   endforeach(f)

   set(${var} "${listVar}" PARENT_SCOPE)

endfunction(PREPEND)

macro(define_env_cache_str_var)

   if (NOT DEFINED ${ARGV0})

      if (NOT "$ENV{${ARGV0}}" STREQUAL "")
         set(${ARGV0} "$ENV{${ARGV0}}" CACHE INTERNAL "")
      else()
         set(${ARGV0} "${ARGV1}" CACHE INTERNAL "")
      endif()

   else()

      if ("$ENV{PERMISSIVE}" STREQUAL "")
         if (NOT "$ENV{${ARGV0}}" STREQUAL "")
            if (NOT "$ENV{${ARGV0}}" STREQUAL "${${ARGV0}}")

               set(msg "")
               string(CONCAT msg "Environment var ${ARGV0}='$ENV{${ARGV0}}' "
                                 "differs from cached value='${${ARGV0}}'. "
                                 "The whole build directory must be ERASED "
                                 "in order to change that.")

               message(FATAL_ERROR "\n${msg}")
            endif()
         endif()
      endif()

   endif()

endmacro()

macro(define_env_cache_bool_var)

   if (NOT DEFINED _CACHE_${ARGV0})

      if (NOT "$ENV{${ARGV0}}" STREQUAL "")
         set(_CACHE_${ARGV0} "$ENV{${ARGV0}}" CACHE INTERNAL "")
      else()
         set(_CACHE_${ARGV0} 0 CACHE INTERNAL "")
      endif()

   else()

      if (NOT "$ENV{${ARGV0}}" STREQUAL "")
         if (NOT "$ENV{${ARGV0}}" STREQUAL "${_CACHE_${ARGV0}}")

            set(msg "")
            string(CONCAT msg "Environment variable ${ARGV0}='$ENV{${ARGV0}}' "
                              "differs from cached value='${${ARGV0}}'. "
                              "The whole build directory must be ERASED "
                              "in order to change that.")

            message(FATAL_ERROR "\n${msg}")
         endif()
      endif()
   endif()

   if (_CACHE_${ARGV0})
      set(${ARGV0} 1)
   else()
      set(${ARGV0} 0)
   endif()

endmacro()

# Truncate the tilck_option() sidecar at configure-time start. Call
# once from the top-level CMakeLists.txt, before any tilck_option()
# call. Subsequent tilck_option() calls (from sub-projects that re-
# include kernel_options.cmake etc.) emit into this file exactly once
# per option via an idempotency guard on a GLOBAL property.
macro(tilck_init_options_sidecar)
   file(WRITE "${CMAKE_BINARY_DIR}/tilck_options.json" "")
   set_property(GLOBAL PROPERTY TILCK_OPTIONS_EMITTED "")
   set_property(GLOBAL PROPERTY TILCK_PENDING_DEP_CHECKS "")
endmacro()

# Enforce deferred BOOL DEPENDS checks recorded by tilck_option().
# Call exactly once, at the end of the root CMakeLists.txt, after
# every tilck_option() invocation has run (including in sub-
# projects that re-include option files). Dep-names can refer to
# other tilck_option()-defined options that didn't exist when the
# dependent was defined — e.g. the MOD_* alphabetical-glob loop
# where MOD_acpi comes before MOD_pci.
macro(tilck_finalize_options_deps)

   get_property(_pending GLOBAL PROPERTY TILCK_PENDING_DEP_CHECKS)
   foreach(_entry ${_pending})

      string(FIND "${_entry}" "|" _sep)
      string(SUBSTRING "${_entry}" 0 ${_sep} _name)
      math(EXPR _deps_start "${_sep} + 1")
      string(SUBSTRING "${_entry}" ${_deps_start} -1 _deps_csv)
      string(REPLACE "," ";" _deps "${_deps_csv}")

      if (NOT ${${_name}})
         continue()   # option disabled — deps don't matter
      endif()

      foreach(_dep ${_deps})
         string(SUBSTRING "${_dep}" 0 1 _dep_first)
         if ("${_dep_first}" STREQUAL "!")
            string(SUBSTRING "${_dep}" 1 -1 _dep_name)
            if (${${_dep_name}})
               message(FATAL_ERROR
                  "tilck_option(${_name}) requires !${_dep_name} "
                  "but ${_dep_name} is set")
            endif()
         else()
            if (NOT ${${_dep}})
               message(FATAL_ERROR
                  "tilck_option(${_name}) requires ${_dep} "
                  "but ${_dep} is not set")
            endif()
         endif()
      endforeach()

   endforeach()

endmacro()

# tilck_option() — declare a user-visible build option with metadata.
#
#   tilck_option(<NAME>
#      TYPE     <BOOL|STRING|ENUM|INT|UINT|ADDR>
#      CATEGORY <slash/path>    # e.g. "Kernel/Memory"
#      DEFAULT  <value>
#      [DEPENDS <expr> ...]     # names, optionally ! prefixed
#      [STRINGS <v1> ...]       # ENUM only
#      HELP     <line> [<line> ...]
#   )
#
# Works like set(... CACHE ...) plus:
#   - $ENV{NAME} override with drift check (same contract as
#     define_env_cache_str_var).
#   - DEFAULT validated against TYPE. Mismatch → FATAL_ERROR.
#   - ENUM exposes STRINGS via set_property(CACHE PROPERTY STRINGS).
#   - DEPENDS enforced at configure time (simple AND of truthiness,
#     with optional leading ! for negation). Complex expressions
#     (&& || parens) are passed through to the sidecar but not
#     enforced by CMake.
#   - Appends one JSONL record to build/tilck_options.json for the
#     menuconfig-style configurator (see scripts/dev/configurator/).
macro(tilck_option NAME)

   cmake_parse_arguments(TO
      ""
      "TYPE;CATEGORY;DEFAULT"
      "DEPENDS;STRINGS;HELP"
      ${ARGN}
   )

   # --- Argument validation ---

   if (NOT TO_TYPE)
      message(FATAL_ERROR "tilck_option(${NAME}): TYPE is required")
   endif()
   if (NOT TO_CATEGORY)
      message(FATAL_ERROR "tilck_option(${NAME}): CATEGORY is required")
   endif()
   if (NOT DEFINED TO_DEFAULT)
      message(FATAL_ERROR "tilck_option(${NAME}): DEFAULT is required")
   endif()
   if (NOT TO_HELP)
      message(FATAL_ERROR "tilck_option(${NAME}): HELP is required")
   endif()

   set(_tilck_valid_types BOOL STRING ENUM INT UINT ADDR)
   list(FIND _tilck_valid_types "${TO_TYPE}" _tidx)
   if (_tidx EQUAL -1)
      message(FATAL_ERROR
         "tilck_option(${NAME}): unknown TYPE '${TO_TYPE}'. "
         "Valid: ${_tilck_valid_types}")
   endif()

   if ("${TO_TYPE}" STREQUAL "ENUM" AND NOT TO_STRINGS)
      message(FATAL_ERROR "tilck_option(${NAME}): ENUM requires STRINGS")
   endif()

   # --- Env override + drift check (mirror define_env_cache_str_var) ---

   if (NOT DEFINED ${NAME})
      if (NOT "$ENV{${NAME}}" STREQUAL "")
         set(_initial_value "$ENV{${NAME}}")
      else()
         set(_initial_value "${TO_DEFAULT}")
      endif()
   else()
      set(_initial_value "${${NAME}}")
      if ("$ENV{PERMISSIVE}" STREQUAL "")
         if (NOT "$ENV{${NAME}}" STREQUAL "")
            if (NOT "$ENV{${NAME}}" STREQUAL "${${NAME}}")
               message(FATAL_ERROR
                  "tilck_option(${NAME}): env var='$ENV{${NAME}}' "
                  "differs from cached value='${${NAME}}'. "
                  "Erase the build directory to change it.")
            endif()
         endif()
      endif()
   endif()

   # --- Type-specific validation + normalisation ---

   if ("${TO_TYPE}" STREQUAL "BOOL")
      if (${_initial_value})
         set(_initial_value "ON")
      else()
         set(_initial_value "OFF")
      endif()
   elseif ("${TO_TYPE}" STREQUAL "INT")
      if (NOT "${_initial_value}" MATCHES "^-?[0-9]+$")
         message(FATAL_ERROR
            "tilck_option(${NAME}): INT value '${_initial_value}' "
            "is not a valid integer")
      endif()
   elseif ("${TO_TYPE}" STREQUAL "UINT")
      if (NOT "${_initial_value}" MATCHES "^[0-9]+$")
         message(FATAL_ERROR
            "tilck_option(${NAME}): UINT value '${_initial_value}' "
            "is not a valid unsigned integer")
      endif()
   elseif ("${TO_TYPE}" STREQUAL "ADDR")
      if (NOT "${_initial_value}" MATCHES "^0x[0-9a-fA-F]+$")
         message(FATAL_ERROR
            "tilck_option(${NAME}): ADDR value '${_initial_value}' "
            "is not a valid 0x-prefixed hex value")
      endif()
   elseif ("${TO_TYPE}" STREQUAL "ENUM")
      list(FIND TO_STRINGS "${_initial_value}" _sidx)
      if (_sidx EQUAL -1)
         message(FATAL_ERROR
            "tilck_option(${NAME}): ENUM value '${_initial_value}' "
            "not in STRINGS (${TO_STRINGS})")
      endif()
   endif()

   # --- Set CACHE variable ---

   if ("${TO_TYPE}" STREQUAL "BOOL")
      set(_tilck_cmake_type "BOOL")
   else()
      set(_tilck_cmake_type "STRING")
   endif()

   list(GET TO_HELP 0 _tilck_help_summary)
   set(${NAME} "${_initial_value}"
       CACHE ${_tilck_cmake_type} "${_tilck_help_summary}")

   if ("${TO_TYPE}" STREQUAL "ENUM")
      set_property(CACHE ${NAME} PROPERTY STRINGS ${TO_STRINGS})
   endif()

   # --- DEPENDS runtime check ---
   #
   # For BOOL options, DEPENDS is a hard invariant: if the option is
   # enabled but any dep is not, that's a configuration error. The
   # check is DEFERRED to tilck_finalize_options_deps() (called at
   # the end of the root CMakeLists.txt) because some option
   # definitions reference OTHER tilck_option()-defined options
   # that may not exist yet at call time — e.g. the MOD_* loop,
   # where MOD_acpi is processed alphabetically before MOD_pci
   # even though it DEPENDS on it.
   #
   # For non-BOOL options (INT/UINT/ADDR/STRING/ENUM) used as
   # conditional sub-options, DEPENDS controls VISIBILITY in mconf
   # only — the cache always holds a valid value, and that value is
   # simply "irrelevant" when the dep is false. CMake does not
   # enforce the dep for these; Kconfig hides the option in the UI
   # and the sidecar carries the DEPENDS through for the generator
   # to emit.
   if ("${TO_TYPE}" STREQUAL "BOOL" AND TO_DEPENDS)
      # Pack (name, deps) into the global pending list using a
      # pipe-separator that can't occur inside option names. The
      # deps list itself stays semicolon-joined for list-rehydration
      # by tilck_finalize_options_deps.
      get_property(_pending GLOBAL PROPERTY TILCK_PENDING_DEP_CHECKS)
      string(REPLACE ";" "," _deps_csv "${TO_DEPENDS}")
      list(APPEND _pending "${NAME}|${_deps_csv}")
      set_property(GLOBAL PROPERTY TILCK_PENDING_DEP_CHECKS "${_pending}")
   endif()

   # --- Emit JSONL record (once per option per configure) ---

   get_property(_emitted GLOBAL PROPERTY TILCK_OPTIONS_EMITTED)
   list(FIND _emitted "${NAME}" _eidx)
   if (_eidx EQUAL -1)
      list(APPEND _emitted "${NAME}")
      set_property(GLOBAL PROPERTY TILCK_OPTIONS_EMITTED "${_emitted}")
      _tilck_option_emit_jsonl(
         "${NAME}" "${TO_TYPE}" "${TO_CATEGORY}"
         "${TO_DEFAULT}" "${_initial_value}"
         "${TO_DEPENDS}" "${TO_STRINGS}" "${TO_HELP}"
      )
   endif()

endmacro()

# Internal helper: emit one JSONL line to the sidecar. HELP (multi-
# line) is joined with literal \n (backslash-n, JSON's newline escape).
# HELP / CATEGORY / string values must not contain " or \ — detected
# and reported via FATAL_ERROR, since full JSON escaping in CMake is
# brittle and every existing option's help text scans clean.
function(_tilck_option_emit_jsonl
         NAME TYPE CATEGORY DFLT CURRENT DEPENDS_LIST STRINGS_LIST HELP_LIST)

   string(TOLOWER "${TYPE}" _json_type)

   foreach(_check_str IN LISTS HELP_LIST DEPENDS_LIST STRINGS_LIST)
      string(FIND "${_check_str}" "\\" _bs)
      string(FIND "${_check_str}" "\"" _q)
      if (NOT _bs EQUAL -1 OR NOT _q EQUAL -1)
         message(FATAL_ERROR
            "tilck_option(${NAME}): HELP/DEPENDS/STRINGS values may "
            "not contain \\ or \" (JSON-escape not implemented).")
      endif()
   endforeach()

   string(REPLACE ";" "\\n" _help_json "${HELP_LIST}")

   set(_deps_json "[")
   set(_first TRUE)
   foreach(_d ${DEPENDS_LIST})
      if (_first)
         set(_first FALSE)
      else()
         string(APPEND _deps_json ",")
      endif()
      string(APPEND _deps_json "\"${_d}\"")
   endforeach()
   string(APPEND _deps_json "]")

   set(_strings_part "")
   if (STRINGS_LIST)
      set(_strings_json "[")
      set(_first TRUE)
      foreach(_s ${STRINGS_LIST})
         if (_first)
            set(_first FALSE)
         else()
            string(APPEND _strings_json ",")
         endif()
         string(APPEND _strings_json "\"${_s}\"")
      endforeach()
      string(APPEND _strings_json "]")
      set(_strings_part ",\"strings\":${_strings_json}")
   endif()

   set(_line "{\"name\":\"${NAME}\"")
   string(APPEND _line ",\"type\":\"${_json_type}\"")
   string(APPEND _line ",\"category\":\"${CATEGORY}\"")
   string(APPEND _line ",\"default\":\"${DFLT}\"")
   string(APPEND _line ",\"current\":\"${CURRENT}\"")
   string(APPEND _line ",\"depends\":${_deps_json}")
   string(APPEND _line "${_strings_part}")
   string(APPEND _line ",\"help\":\"${_help_json}\"}")

   file(APPEND "${CMAKE_BINARY_DIR}/tilck_options.json" "${_line}\n")

endfunction()

# tilck_option_comment() — emit a non-interactive menu separator.
#
#   tilck_option_comment(<TEXT> CATEGORY <category>)
#
# Adds a sidecar record with type="comment" that the generator
# renders as a Kconfig `comment "TEXT"` line. In mconf this shows
# as `--- TEXT ---` in the menu — a lightweight way to group
# related options on the same screen without nesting a sub-menu.
# No cache variable is set; there is no value to store.
macro(tilck_option_comment TEXT)

   cmake_parse_arguments(TOC "" "CATEGORY" "" ${ARGN})

   if (NOT TOC_CATEGORY)
      message(FATAL_ERROR
         "tilck_option_comment(${TEXT}): CATEGORY is required")
   endif()

   foreach(_check IN ITEMS "${TEXT}" "${TOC_CATEGORY}")
      string(FIND "${_check}" "\\" _bs)
      string(FIND "${_check}" "\"" _q)
      if (NOT _bs EQUAL -1 OR NOT _q EQUAL -1)
         message(FATAL_ERROR
            "tilck_option_comment(${TEXT}): TEXT/CATEGORY may not "
            "contain \\ or \".")
      endif()
   endforeach()

   file(APPEND "${CMAKE_BINARY_DIR}/tilck_options.json"
      "{\"type\":\"comment\",\"category\":\"${TOC_CATEGORY}\",\"text\":\"${TEXT}\"}\n"
   )

endmacro()

macro(set_cross_compiler_internal)

   set(CMAKE_C_COMPILER ${ARGV0}/${ARGV1}-linux-gcc)
   set(CMAKE_CXX_COMPILER ${ARGV0}/${ARGV1}-linux-g++)
   set(CMAKE_ASM_COMPILER ${ARGV0}/${ARGV1}-linux-gcc)
   set(CMAKE_OBJCOPY ${ARGV0}/${ARGV1}-linux-objcopy)
   set(CMAKE_STRIP ${ARGV0}/${ARGV1}-linux-strip)
   set(CMAKE_AR ${ARGV0}/${ARGV1}-linux-ar)
   set(CMAKE_RANLIB ${ARGV0}/${ARGV1}-linux-ranlib)
   set(TOOL_GCOV ${ARGV0}/${ARGV1}-linux-gcov)

endmacro()

macro(set_cross_compiler)

   set(CMAKE_C_FLAGS "${ARCH_GCC_FLAGS}")
   set(CMAKE_CXX_FLAGS "${ARCH_GCC_FLAGS} ${KERNEL_CXX_FLAGS}")
   set(CMAKE_ASM_FLAGS "${ARCH_GCC_FLAGS}")

   set_cross_compiler_internal(${GCC_TOOLCHAIN} ${ARCH_GCC_TC})

endmacro()

macro(set_cross_compiler_userapps)

   set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-gcc)
   set(CMAKE_CXX_COMPILER ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-g++)
   set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-gcc)
   set(CMAKE_OBJCOPY ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-objcopy)
   set(CMAKE_STRIP ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-strip)
   set(CMAKE_AR ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-ar)
   set(CMAKE_RANLIB ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-ranlib)
   set(TOOL_GCOV ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-gcov)

endmacro()


#message("CMAKE_ASM_COMPILE_OBJECT: ${CMAKE_ASM_COMPILE_OBJECT}")
#message("CMAKE_C_LINK_EXECUTABLE: ${CMAKE_C_LINK_EXECUTABLE}")
#message("CMAKE_C_COMPILE_OBJECT: ${CMAKE_C_COMPILE_OBJECT}")
#message("CMAKE_C_LINK_FLAGS: ${CMAKE_C_LINK_FLAGS}")
#message("CMAKE_CXX_COMPILE_OBJECT: ${CMAKE_CXX_COMPILE_OBJECT}")


function(smart_config_file src dest)

   configure_file(
      ${src}
      ${dest}.tmp
      @ONLY
   )

   execute_process(

      COMMAND
         ${CMAKE_COMMAND} -E compare_files ${dest}.tmp ${dest}

      RESULT_VARIABLE
         NEED_UPDATE

      OUTPUT_QUIET
      ERROR_QUIET
   )

   if(NEED_UPDATE)

      execute_process(
         COMMAND ${CMAKE_COMMAND} -E rename ${dest}.tmp ${dest}
      )

   else()

      execute_process(
         COMMAND ${CMAKE_COMMAND} -E rm ${dest}.tmp
      )

   endif()

   set_source_files_properties(${dest} PROPERTIES GENERATED TRUE)

endfunction()
