# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

X11_MIRROR = "https://xorg.freedesktop.org/archive/individual"

#
# The X11 client libraries GTK needs.
#
# Fourteen packages that differ only in their name, their upstream
# directory, what they depend on and which file proves they installed.
# Everything else — our compiler, --prefix naming the sysroot, DESTDIR
# staging, the sysroot-shaped fragment, what they publish to
# dependents — is identical, so they are a TABLE and one class rather
# than fourteen near-identical files.
#
# Writing them out individually would mean fourteen copies of the same
# forty lines, and a change to the shared shape would have to be made
# fourteen times.
#
# `upstream` is separate from the package name because the tarballs are
# capitalised (libX11-1.8.13.tar.xz) while our names are not
# (host_libx11 -> HOST_VER_LIBX11).
#
X11_LIBS = [
  # Headers and protocol descriptions: no code, nothing to link.
  { name: "xorgproto", upstream: "xorgproto", dir: "proto",
    check: "usr/include/X11/X.h", deps: [] },

  { name: "xtrans", upstream: "xtrans", dir: "lib",
    check: "usr/include/X11/Xtrans/Xtrans.h", deps: [] },

  { name: "xcb_proto", upstream: "xcb-proto", dir: "proto",
    check: "usr/share/xcb/xproto.xml", deps: [] },

  # The bottom of the client stack.
  { name: "libxau", upstream: "libXau", dir: "lib",
    check: "usr/lib/libXau.so", deps: ["host_xorgproto"] },

  { name: "libxdmcp", upstream: "libXdmcp", dir: "lib",
    check: "usr/lib/libXdmcp.so", deps: ["host_xorgproto"] },

  { name: "libxcb", upstream: "libxcb", dir: "lib",
    check: "usr/lib/libxcb.so",
    deps: ["host_xcb_proto", "host_libxau", "host_libxdmcp"] },

  { name: "libx11", upstream: "libX11", dir: "lib",
    check: "usr/lib/libX11.so",
    deps: ["host_libxcb", "host_xtrans", "host_xorgproto"] },

  # Extensions, all on top of libX11.
  { name: "libxext", upstream: "libXext", dir: "lib",
    check: "usr/lib/libXext.so", deps: ["host_libx11"] },

  { name: "libxrender", upstream: "libXrender", dir: "lib",
    check: "usr/lib/libXrender.so", deps: ["host_libx11"] },

  { name: "libxfixes", upstream: "libXfixes", dir: "lib",
    check: "usr/lib/libXfixes.so", deps: ["host_libx11"] },

  { name: "libxi", upstream: "libXi", dir: "lib",
    check: "usr/lib/libXi.so",
    deps: ["host_libxext", "host_libxfixes"] },

  { name: "libxrandr", upstream: "libXrandr", dir: "lib",
    check: "usr/lib/libXrandr.so",
    deps: ["host_libxext", "host_libxrender"] },

  { name: "libxcursor", upstream: "libXcursor", dir: "lib",
    check: "usr/lib/libXcursor.so",
    deps: ["host_libxrender", "host_libxfixes"] },

  { name: "libxinerama", upstream: "libXinerama", dir: "lib",
    check: "usr/lib/libXinerama.so", deps: ["host_libxext"] },
].freeze

class X11Package < Package

  include FileShortcuts
  include FileUtilsShortcuts

  attr_reader :check_file

  def initialize(spec)

    @check_file = spec[:check]
    upstream = spec[:upstream]

    src = SourceRef.new(
      name: spec[:name],
      url: "#{X11_MIRROR}/#{spec[:dir]}",
      tarname: ->(ver) { "#{upstream}-#{ver}.tar.xz" },
      fetch_via_git: false,
    )

    deps = spec[:deps].map { |d| Dep(d, true) }
    deps << Dep('host_gcc', true)

    super(
      name: "host_#{spec[:name]}",
      source: src,
      on_host: true,
      is_compiler: false,
      host_tier: :hermetic,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: deps,
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HERMETIC_ENABLED

  def expected_files(ver = nil) = [["install/#{@check_file}", false]]

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

  def install_impl_internal(install_dir)

    return autotools_hermetic_build(install_dir, args: [
      "--disable-static",
      "--libdir=#{hermetic_sysroot}/usr/lib",

      # X.org's configure scripts look for their own .m4 macros and for
      # sibling headers under the prefix; without this they find the
      # host's copies in /usr/share and mix the two.
      "--datarootdir=#{hermetic_sysroot}/usr/share",
    ])
  end
end

X11_LIBS.each { |spec| pkgmgr.register(X11Package.new(spec)) }
