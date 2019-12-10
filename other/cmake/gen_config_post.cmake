# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

set(BUILD_FATPART ${CMAKE_BINARY_DIR}/scripts/build_fatpart)
set(BUILD_TEST_FATPART ${CMAKE_BINARY_DIR}/scripts/build_test_fatpart)

configure_file(
   ${CMAKE_SOURCE_DIR}/scripts/templates/build_fatpart
   ${BUILD_FATPART}
   @ONLY
)

configure_file(
   ${CMAKE_SOURCE_DIR}/scripts/templates/build_test_fatpart
   ${BUILD_TEST_FATPART}
   @ONLY
)
