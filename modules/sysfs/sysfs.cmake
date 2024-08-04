# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

set(
   GENERATED_CONFIG_FILE
   ${CMAKE_BINARY_DIR}/tilck_gen_headers/generated_config.h
)

add_custom_command(

   OUTPUT
      ${GENERATED_CONFIG_FILE}

   COMMAND
      ${BUILD_APPS}/gen_config ${CMAKE_SOURCE_DIR} ${GENERATED_CONFIG_FILE}

   DEPENDS
      gen_config
)

add_custom_target(

   generated_configuration${TARGET_VARIANT}

   DEPENDS
      ${GENERATED_CONFIG_FILE}
)

add_dependencies(${TARGET_NAME} generated_configuration)
build_and_link_module(${TARGET_NAME} "sysfs" ${TARGET_VARIANT})
