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
# meson-built package of the hermetic stack.
#
# zlib established the pattern for a hand-written configure; this
# establishes it for meson, which most of the GTK closure uses. The
# hermetic parts are identical either way — our compiler via
# with_hermetic_toolchain, --prefix naming the sysroot, DESTDIR
# staging, a sysroot-shaped fragment — and only the invocation differs.
#
# meson picks the compiler up from CC/CXX, which with_hermetic_toolchain
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
      host_tier: :hermetic,
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
  def enabled? = HERMETIC_ENABLED

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

  def install_impl_internal(install_dir)

    sysroot_usr = "#{hermetic_sysroot}/usr"
    destdir = "#{install_dir}/destdir"

    ok = false

    # meson and ninja are on PATH because they publish their bin dirs
    # and with_hermetic_toolchain applies what the dependencies say.
    # Nothing here names those packages or guesses where they live.
    with_hermetic_toolchain do
      ok = run_command("configure.log", [
        "meson", "setup", "build",
        "--prefix=#{sysroot_usr}",
        "--libdir=lib",              # not lib64: the sysroot has one libdir
        "--buildtype=release",
        "-Dtests=disabled",          # nothing here consumes them
        "-Ddemos=disabled",
      ])
      next if !ok

      ok = run_command("build.log", ["ninja", "-C", "build"])
      next if !ok

      ok = run_command("install.log",
                       ["meson", "install", "-C", "build",
                        "--destdir=#{destdir}"])
    end

    return false if !ok

    FileUtils.mkdir_p("#{install_dir}/install")
    FileUtils.mv("#{destdir}#{sysroot_usr}", "#{install_dir}/install/usr")

    Dir.children(".").each { |e|
      next if e == "install"
      rm_rf(e)
    }
    return true
  end
end

pkgmgr.register(HostPixmanPackage.new())
