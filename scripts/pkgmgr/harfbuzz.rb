# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

HARFBUZZ_SOURCE = SourceRef.new(
  name: 'harfbuzz',
  url:  GITHUB + '/harfbuzz/harfbuzz/releases/download',
  tarname: ->(ver) { "harfbuzz-#{ver}.tar.xz" },
  remote_tarname: ->(ver) { "#{ver}/harfbuzz-#{ver}.tar.xz" },
  fetch_via_git: false,
)

#
# host_harfbuzz: text shaping — turning a string and a font into
# positioned glyphs. pango's whole purpose sits on top of it.
#
# Built after freetype, which was built without harfbuzz to break the
# mutual dependency between them. See the note in freetype.rb.
#
class HostHarfbuzzPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_harfbuzz',
      source: HARFBUZZ_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :hermetic,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_glib2', true),
        Dep('host_freetype', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HERMETIC_ENABLED

  def expected_files(ver = nil) = [
    ["install/usr/lib/libharfbuzz.so", false],
    ["install/usr/include/harfbuzz/hb.h", false],
    ["install/usr/lib/pkgconfig/harfbuzz.pc", false],
  ]

  def build_env(ver)
    prefix = install_prefix(ver) / "install" / "usr"
    return BuildEnv.new(
      include_dirs:    [prefix / "include" / "harfbuzz"],
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
    return meson_hermetic_build(install_dir, args: [
      "-Dfreetype=enabled",
      "-Dglib=enabled",
      "-Dtests=disabled",
      "-Ddocs=disabled",
      "-Dutilities=disabled",
      "-Dcairo=disabled",      # cairo comes later and does not need it
      "-Dicu=disabled",
    ])
  end
end

pkgmgr.register(HostHarfbuzzPackage.new())
