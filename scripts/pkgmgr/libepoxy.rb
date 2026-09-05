# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

LIBEPOXY_SOURCE = SourceRef.new(
  name: 'libepoxy',
  url:  GITHUB + '/anholt/libepoxy/archive/refs/tags',
  tarname: ->(ver) { "libepoxy-#{ver}.tar.gz" },
  remote_tarname: ->(ver) { "#{ver}.tar.gz" },
  fetch_via_git: false,
)

#
# host_libepoxy: OpenGL function pointer management. GTK 3 links it
# unconditionally — GdkGLContext is part of the API whether or not
# anything ever asks for a GL surface.
#
# Nothing from Mesa is needed to BUILD it: epoxy generates its
# dispatch tables from the Khronos XML registry shipped in the
# tarball, and resolves the real entry points with dlopen at runtime.
# So this adds a build dependency on nothing, and a runtime one only
# on machines that actually create a GL context.
#
# EGL is off for that reason — it would want Mesa's headers at build
# time. GLX is enough for the X11 backend.
#
class HostLibepoxyPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_libepoxy',
      source: LIBEPOXY_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_libx11', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def pkg_dirname = "libepoxy"

  def expected_files(ver = nil) = [
    ["install/usr/lib/libepoxy.so", false],
    ["install/usr/lib/pkgconfig/epoxy.pc", false],
  ]

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
    FileUtils.rm_rf(dir / "build")
    super(dir)
  end

  def build_flags(ver = nil) = [
      "-Dglx=yes",
      "-Degl=no",
      "-Dx11=true",
      "-Dtests=false",
  ]

  def install_impl_internal(install_dir)
    return meson_stack_build(install_dir)
  end
end

pkgmgr.register(HostLibepoxyPackage.new())
