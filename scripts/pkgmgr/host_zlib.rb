# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'
require_relative 'zlib'   # for ZLIB_SOURCE

#
# host_zlib: the first ordinary library of the host stack, and the
# template the rest of the QEMU closure follows.
#
# The pattern, which is what this package exists to establish:
#
#   * built with OUR compiler, via with_stack_toolchain. This is the
#     part that makes it portable; the sysroot alone would not, since
#     the system gcc would happily link the system libc against it;
#   * --prefix names the SYSROOT, not this package's directory, so
#     every absolute path baked into the result is the path the symlink
#     farm will make true. Installed through DESTDIR so the tree handed
#     to the atomic move is complete;
#   * the install is a sysroot-shaped fragment (install/usr/...), which
#     is what lets the farm merge it with no special case;
#   * build_env publishes what dependents need, so nothing downstream
#     ever names this package.
#
# It shares ZLIB_SOURCE with the target zlib and their versions are
# unrelated: Tilck ships v1.2.11 while the host builds a current one.
# One tarball per version in the cache, and neither side constrains the
# other — which is the whole point of the two version files.
#
# See docs/plans/portable-host-stack.md.
#
class HostZlibPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_zlib',
      source: ZLIB_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [Dep('host_gcc', true)],
      default: false,
    )
  end

  def default_arch = HOST_ARCH

  def expected_files(ver = nil) = [
    ["install/usr/lib/libz.so", false],
    ["install/usr/lib/libz.a", false],
    ["install/usr/include/zlib.h", false],
    ["install/usr/lib/pkgconfig/zlib.pc", false],
  ]

  # What dependents get. They never name this package: they declare a
  # dependency and deps_build_env collects this.
  def build_env(ver)

    prefix = install_prefix(ver) / "install" / "usr"

    return BuildEnv.new(
      include_dirs:    [prefix / "include"],
      lib_dirs:        [prefix / "lib"],
      pkg_config_dirs: [prefix / "lib" / "pkgconfig"],
    )
  end

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    super(dir)
  end

  # zlib's configure is hand-written rather than autotools, but it
  # takes --prefix and its compiler from $CC, so the shared helper
  # drives it unchanged -- and this way the flags are declared and
  # recorded like every other package's.
  def install_impl_internal(install_dir)
    return autotools_stack_build(install_dir)
  end
end

pkgmgr.register(HostZlibPackage.new())
