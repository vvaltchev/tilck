# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

PIXMAN_SOURCE = SourceRef.new(
  name: 'pixman',
  url:  'https://cairographics.org/releases',
  tarname: ->(ver) { "pixman-#{ver}.tar.xz" },
)

#
# host_pixman: QEMU's pixel manipulation library, and the first
# meson-built package of the host stack.
#
# zlib established the pattern for a hand-written configure; this
# establishes it for meson, which most of the GTK closure uses. The
# portable parts are identical either way — our compiler via
# with_stack_toolchain, --prefix naming the sysroot, DESTDIR
# staging, a sysroot-shaped fragment — and only the invocation differs.
#
# meson picks the compiler up from CC/CXX, which with_stack_toolchain
# sets, and finds dependencies through PKG_CONFIG_LIBDIR, which it
# points at the sysroot alone.
#
class HostPixmanPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_pixman',
      source: PIXMAN_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"

  def expected_files(ver = nil) = [
    ["install/usr/lib/libpixman-1.so", false],
    ["install/usr/include/pixman-1/pixman.h", false],
    ["install/usr/lib/pkgconfig/pixman-1.pc", false],
  ]

  def build_env(ver)

    prefix = install_prefix(ver) / "install" / "usr"

    return BuildEnv.new(
      include_dirs:    [prefix / "include" / "pixman-1"],
      lib_dirs:        [prefix / "lib"],
      pkg_config_dirs: [prefix / "lib" / "pkgconfig"],
    )
  end

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    FileUtils.rm_rf(dir / "build")
    super(dir)
  end

  def build_flags(ver = nil) = [
      "-Dtests=disabled",          # nothing here consumes them
      "-Ddemos=disabled",
  ]

  def install_impl_internal(install_dir)

    return meson_stack_build(install_dir)
  end

end

pkgmgr.register(HostPixmanPackage.new())
