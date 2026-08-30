# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

PANGO_SOURCE = SourceRef.new(
  name: 'pango',
  url:  'https://download.gnome.org/sources/pango',
  tarname: ->(ver) { "pango-#{ver}.tar.xz" },
  remote_tarname: ->(ver) {
    series = ver.to_s.split(".")[0, 2].join(".")
    "#{series}/pango-#{ver}.tar.xz"
  },
  fetch_via_git: false,
)

#
# host_pango: text layout — the layer that turns a paragraph, a font
# description and a width into positioned lines. Every piece of text
# GTK 3 draws goes through it.
#
# The 1.5x series rather than the newest: 1.90 is the development line
# toward pango 2, whose API GTK 3 does not target. GTK 3.24.52 asks for
# >= 1.41, so this is far ahead of the requirement either way.
#
# Sits on the whole text stack below it — harfbuzz for shaping, fribidi
# for bidirectional text, freetype and fontconfig for fonts, cairo for
# rendering — and publishes pangocairo and pangoft2 alongside pango
# itself, which is what GTK actually links.
#
class HostPangoPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_pango',
      source: PANGO_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_glib2', true),
        Dep('host_harfbuzz', true),
        Dep('host_fribidi', true),
        Dep('host_fontconfig', true),
        Dep('host_freetype', true),
        Dep('host_cairo', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HOST_STACK_ENABLED

  def expected_files(ver = nil) = [
    ["install/usr/lib/libpango-1.0.so", false],
    ["install/usr/lib/libpangocairo-1.0.so", false],
    ["install/usr/lib/pkgconfig/pango.pc", false],
    ["install/usr/lib/pkgconfig/pangocairo.pc", false],
  ]

  def build_env(ver)
    prefix = install_prefix(ver) / "install" / "usr"
    return BuildEnv.new(
      include_dirs:    [prefix / "include" / "pango-1.0"],
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
      "-Dintrospection=disabled",
      "-Ddocumentation=false",
      "-Dbuild-testsuite=false",
      "-Dbuild-examples=false",
  ]

  def install_impl_internal(install_dir)
    return meson_stack_build(install_dir)
  end
end

pkgmgr.register(HostPangoPackage.new())
