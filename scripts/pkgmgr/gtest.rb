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

    ok = @source.download(ver)
    return false if !ok

    # Extract source into staging. Staging is a scratch area — the final
    # installed package keeps only the `install/` tree produced by cmake.
    staging = staging_dir(ver)
    FileUtils.rm_rf(staging)
    chdir_package_base_dir(TC_STAGING) do
      ok = @source.extract(ver, ver_dirname(ver))
      return false if !ok
    end

    ver_dir = host_install_root / pkg_dirname / ver_dirname(ver)
    build_dir = ver_dir / "build"
    install_dir = ver_dir / "install"
    FileUtils.mkdir_p(build_dir)

    ok = FileUtils.chdir(build_dir) do
      install_impl_internal(staging, install_dir)
    end

    if ok
      FileUtils.rm_rf(build_dir)
      FileUtils.rm_rf(staging)
      staging_pkg = TC_STAGING / pkg_dirname
      FileUtils.rmdir(staging_pkg) if staging_pkg.directory? &&
                                      Dir.empty?(staging_pkg)
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

pkgmgr.register(GtestPackage.new())
