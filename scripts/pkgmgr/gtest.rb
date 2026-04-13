# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

GTEST_URL = GITHUB + '/google/googletest'

#
# Source-only (noarch) googletest: the shared source tree used by
# host_gtest. Lives in <toolchain>/noarch/gtest/<ver>/.
#
class GtestSrcPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'gtest_src',
      url: GTEST_URL,
      on_host: false,
      is_compiler: false,
      arch_list: nil,      # noarch
      dep_list: []
    )
  end

  def pkg_dirname = "gtest"
  def tarname(ver) = "gtest-#{ver}.tgz"
  def git_tag(ver) = "release-#{ver}"
  def default_ver = pkgmgr.get_config_ver("gtest")
  def expected_files = [
    ["googletest", true],
    ["googlemock", true],
    ["CMakeLists.txt", false],
  ]
  def default_arch = nil
  def default_cc = nil

  def install_impl_internal(ignored = nil)
    return true
  end
end

#
# Built gtest+gmock libraries: cmake build of the full googletest tree
# (which includes both gtest and gmock) from the noarch source. Uses
# `cmake --install` to produce a clean install/ tree containing only
# headers and libraries. Built with the host compiler; non-portable
# (dynamically linked against libstdc++), so installed under
# host/<os>-<arch>/<distro>/<host-cc>/gtest/<ver>/install/.
#
class GtestPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_gtest',
      url: nil,
      on_host: true,
      is_compiler: false,
      arch_list: ALL_HOST_ARCHS,
      dep_list: [Dep('gtest_src', false)]
    )
  end

  def pkg_dirname = "gtest"
  def default_ver = pkgmgr.get_config_ver("gtest")
  def default_arch = HOST_ARCH
  def default_cc = "syscc"

  def expected_files = [
    ["install/lib/libgtest.a", false],
    ["install/lib/libgmock.a", false],
    ["install/include/gtest", true],
    ["install/include/gmock", true],
  ]

  def install_impl(ver)

    info "Install #{name} version: #{ver}"

    if installed? ver
      info "Package already installed, skip"
      return nil
    end

    ver_dir = host_install_root / pkg_dirname / ver_dirname(ver)
    build_dir = ver_dir / "build"
    install_dir = ver_dir / "install"
    src_dir = TC_NOARCH / "gtest" / ver_dirname(ver)
    FileUtils.mkdir_p(build_dir)

    ok = FileUtils.chdir(build_dir) do
      install_impl_internal(src_dir, install_dir)
    end

    if ok
      FileUtils.rm_rf(build_dir)
      ok = check_install_dir(ver_dir, true)
    end

    return ok
  end

  def install_impl_internal(src_dir, install_dir)

    # Explicitly pass the host compiler to cmake. On FreeBSD the
    # default `cc`/`c++` are clang, but the rest of the build uses
    # GCC (from ports). A mismatch produces libc++/libstdc++ link
    # errors when the gtests binary is linked.
    cc  = ENV["CC"]  || "gcc"
    cxx = ENV["CXX"] || "g++"

    ok = run_command("cmake.log", [
      "cmake",
      "-DCMAKE_C_COMPILER=#{cc}",
      "-DCMAKE_CXX_COMPILER=#{cxx}",
      "-DCMAKE_BUILD_TYPE=Debug",
      "-DCMAKE_INSTALL_PREFIX=#{install_dir}",
      "-DGOOGLETEST_VERSION=#{default_ver}",
      src_dir.to_s,
    ])
    return false if !ok

    ok = run_command("build.log", [
      "make", "-j#{BUILD_PAR}",
    ])
    return false if !ok

    ok = run_command("install.log", [
      "cmake", "--install", ".",
    ])
    return false if !ok

    return true
  end
end

pkgmgr.register(GtestSrcPackage.new())
pkgmgr.register(GtestPackage.new())
