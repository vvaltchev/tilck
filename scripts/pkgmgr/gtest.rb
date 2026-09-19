# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

GTEST_URL = GITHUB + '/google/googletest'

GTEST_SOURCE = SourceRef.new(
  name: 'gtest',
  url:  GTEST_URL,
  git_tag: ->(ver) { "v#{ver}" },
)

#
# Built gtest+gmock libraries: cmake build of the full googletest tree
# (which includes both gtest and gmock). Uses `cmake --install` to
# produce a clean install/ tree containing only headers and libraries.
# Built with the host compiler; non-portable (dynamically linked against
# libstdc++), so installed under
# host/<os>-<arch>/<distro>/<host-cc>/gtest/<ver>/install/.
#
# The source is extracted into staging for the duration of the build
# and discarded on success — host_gtest is the only consumer, so there
# is no value in keeping a persistent source tree around.
#
class GtestPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_gtest',
      source: GTEST_SOURCE,
      on_host: true,
      is_compiler: false,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: []
    )
  end

  def pkg_dirname = "gtest"

  def expected_files(ver = nil) = [
    ["install/lib/libgtest.a", false],
    ["install/lib/libgmock.a", false],
    ["install/include/gtest", true],
    ["install/include/gmock", true],
  ]

  # This package had an install_impl of its own that built in the
  # version directory with the source left in staging and never went
  # through the atomic move. It predated staging; nothing about gtest
  # needs it. An ordinary out-of-tree cmake build now.
  #
  # The host compiler passed to cmake explicitly: on FreeBSD the
  # default cc/c++ are clang while the rest of the build uses GCC from
  # ports, and a mismatch is libc++/libstdc++ link errors when gtests
  # links. HOST_CC_CMD is the owner of that answer, where the old code
  # read whatever ENV["CC"] happened to hold.
  #
  # CMAKE_INSTALL_LIBDIR=lib, so libgtest.a lands at install/lib/ on
  # every distro: Fedora's cmake defaults to lib64, which would break
  # the expected_files check.
  def build_steps(ver = default_ver) = [
    Mkdir(path: "build"),
    Within(dir: "build", steps: [
      Run(log: "cmake.log", argv: [
        "cmake",
        "-DCMAKE_C_COMPILER=#{HOST_CC_CMD}",
        "-DCMAKE_CXX_COMPILER=#{HOST_CXX_CMD}",
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DCMAKE_INSTALL_PREFIX=$INSTALL/install",
        "-DCMAKE_INSTALL_LIBDIR=lib",
        "-DGOOGLETEST_VERSION=#{ver}",
        "..",
      ]),
      Run(log: "build.log", argv: ["make", "-j$PAR"]),
      Run(log: "install.log", argv: ["cmake", "--install", "."]),
    ]),

    # cmake writes the configured prefix into the .pc files, and the
    # configured prefix is the staging tree. The install this replaces
    # configured with the final path and so left them naming where
    # gtest would live; these do the same, by rewrite. The cmake
    # package files need none of this: they locate the prefix from
    # their own position.
    Substitute(path: "$INSTALL/install/lib/pkgconfig/*.pc", subs: [
      ["$INSTALL/install", "$PREFIX"],
    ]),
    Prune(),
  ]
end

pkgmgr.register(GtestPackage.new())
