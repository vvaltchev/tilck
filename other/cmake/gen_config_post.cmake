# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

set(BUILD_BOOTPART ${CMAKE_BINARY_DIR}/scripts/build_bootpart)
set(BUILD_FATPART ${CMAKE_BINARY_DIR}/scripts/build_fatpart)
set(BUILD_TEST_FATPART ${CMAKE_BINARY_DIR}/scripts/build_test_fatpart)

smart_config_file(
   ${CMAKE_SOURCE_DIR}/scripts/templates/build_bootpart
   ${BUILD_BOOTPART}
)

smart_config_file(
   ${CMAKE_SOURCE_DIR}/scripts/templates/build_fatpart
   ${BUILD_FATPART}
)

smart_config_file(
   ${CMAKE_SOURCE_DIR}/scripts/templates/build_test_fatpart
   ${BUILD_TEST_FATPART}
)
