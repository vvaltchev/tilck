# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'
require_relative 'system_deps'
require_relative 'cargo_build'

GLYCIN_SOURCE = SourceRef.new(
  name: 'glycin',
  url:  'https://download.gnome.org/sources/glycin',
  tarname: ->(ver) { "glycin-#{ver}.tar.xz" },
  remote_tarname: ->(ver) {
    series = ver.to_s.split(".")[0, 2].join(".")
    "#{series}/glycin-#{ver}.tar.xz"
  },
  fetch_via_git: false,
)

#
# glycin: sandboxed image loading, and the Rust corner of the tree.
#
# gdk-pixbuf 2.44 asks for it by pkg-config — `glycin-2 >=
# 2.2.alpha.7` — rather than bundling it. Only prereleases of 2.2
# exist upstream; that is what the requirement names and there is no
# final release to wait for.
#
# Images are not decoded in-process. Each format is handled by a
# separate process confined with seccomp, on the reasoning that an
# image parser is where untrusted input meets C. Hence libseccomp,
# and hence the split below.
#
# TWO PACKAGES FROM ONE TARBALL, and the split is forced rather than
# tidy-minded. Upstream ships libglycin/ (the C library) and
# glycin-loaders/ (the decoder binaries) separately, and they sit on
# opposite sides of a dependency cycle:
#
#   gdk-pixbuf-2.0.pc  Requires: glycin-2      (gdk-pixbuf links it)
#   glycin-svg loader  needs gdk-pixbuf-2.0    (through its crates)
#
# Built as one package that is unsatisfiable, and it fails exactly
# there:
#
#   Package 'glycin-2', required by 'gdk-pixbuf-2.0', not found
#   The system library `gdk-pixbuf-2.0` required by crate
#   `gdk-pixbuf-sys` was not found
#
# Split, the cycle disappears because only the LOADERS need
# gdk-pixbuf: libglycin -> gdk-pixbuf -> glycin-loaders, in that
# order. Distributions build them in the same three steps.
#
# DECLARED PORTABLE, and getting there is the interesting part. Rust
# links with the system cc by default, which would produce libraries
# against the system glibc: not merely untidy, but unusable on a host
# whose glibc is newer than ours, and enough to drag gdk-pixbuf, GTK
# and QEMU out of the portable tree behind them. See CargoBuild for
# how the linker is redirected without breaking the proc-macros.
# Measured on the result, under a hostile LD_LIBRARY_PATH:
#
#   libc.so.6        => <sysroot>/usr/lib/libc.so.6
#   libseccomp.so.2  => <sysroot>/usr/lib/libseccomp.so.2
#   libglib-2.0.so.0 => <sysroot>/usr/lib/libglib-2.0.so.0
#
# with nothing resolving outside toolchain4. The audit at the end of
# each install is what keeps it that way.
#
class GlycinPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts
  include CargoBuild

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HOST_STACK_ENABLED

  # Both halves come out of one tarball and are always the same
  # version, so they share a single entry in the version file even
  # though they install side by side under different names.
  def default_ver = pkgmgr.get_config_ver("glycin", host: true)

  def system_deps(ver = nil) = rust_system_deps

  def build_env(ver)
    prefix = install_prefix(ver) / "install" / "usr"
    return BuildEnv.new(
      include_dirs:    [prefix / "include"],
      lib_dirs:        [prefix / "lib"],
      pkg_config_dirs: [prefix / "lib" / "pkgconfig"],
      bin_dirs:        [prefix / "bin"],
    )
  end

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    FileUtils.rm_rf(dir / "build")
    super(dir)
  end

  # The 2.2 prerelease tarballs ship no po/ directory at all, while
  # glycin-loaders/meson.build merges translations unconditionally
  # through i18n.merge_file -- so every one of them fails with
  # "msgfmt: ../po/LINGUAS does not exist". An empty LINGUAS is what
  # "no translations" looks like to meson, which is accurate: the
  # tarball contains none.
  def ensure_po_dir
    return if File.exist?("po/LINGUAS")
    info "#{name}: creating the po/ directory absent from the tarball"
    FileUtils.mkdir_p("po")
    File.write("po/LINGUAS", "")
  end

  # Options shared by both halves. glycin's meson passes --target to
  # cargo only under meson.is_cross_build(), so the cross file is not
  # optional here -- see CargoBuild#with_cargo_env for why --target
  # decides whether the proc-macros survive.
  # The flags both halves share; each subclass adds which half to
  # build. Declared rather than passed, so the record of what was
  # built cannot omit them -- the cross file included, since which
  # cross file was used decides whether the proc-macros linked
  # against our glibc.
  def build_flags(ver = nil) = [
    "--cross-file=#{cargo_cross_file_path}",
    "-Dlibglycin-gtk4=false",
    "-Dglycin-thumbnailer=false",
    "-Dintrospection=false",
    "-Dvapi=false",
    "-Dtests=false",
    "-Dpython_tests=false",
    *half_flags,
  ]

  def build_halves(install_dir)

    ensure_po_dir
    write_cargo_cross_file

    with_cargo_env { meson_stack_build(install_dir) }
  end
end

#
# The C library gdk-pixbuf links against. Knows nothing of gdk-pixbuf
# itself, which is what keeps the cycle open.
#
class HostLibglycinPackage < GlycinPackage

  def initialize
    super(
      name: 'host_libglycin',
      source: GLYCIN_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_glib2', true),
        Dep('host_cairo', true),
        Dep('host_fontconfig', true),
        Dep('host_libseccomp', true),
      ],
      default: false,
    )
  end

  def pkg_dirname = "libglycin"

  def expected_files(ver = nil) = [
    ["install/usr/lib/libglycin-2.so", false],
    ["install/usr/lib/pkgconfig/glycin-2.pc", false],
  ]

  def half_flags = [
    "-Dlibglycin=true",
    "-Dglycin-loaders=false",
  ]

  def install_impl_internal(install_dir)
    return build_halves(install_dir)
  end
end

#
# The decoders. Separate processes rather than a library, which is why
# nothing links them and why they may depend on gdk-pixbuf freely.
#
# Two of the four loaders upstream builds by default. glycin-image-rs
# is pure Rust and covers PNG, JPEG, GIF and WebP; glycin-svg covers
# SVG, which is what a GTK icon theme is mostly made of — Adwaita
# ships 555 of them — and without which GTK logs "Could not load a
# pixbuf from icon theme" and draws nothing where those icons belong.
#
# The gtk4 that glycin-loaders/meson.build mentions is NOT required
# for glycin-svg: that dependency sits inside `if get_option('tests')`,
# which is off. The other two loaders stay out, each being a codec
# stack of its own — glycin-heif wants libheif, glycin-jxl wants
# libjxl.
#
class HostGlycinLoadersPackage < GlycinPackage

  LOADERS = ["glycin-image-rs", "glycin-svg"].freeze

  def initialize
    super(
      name: 'host_glycin_loaders',
      source: GLYCIN_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_glib2', true),
        Dep('host_cairo', true),
        Dep('host_libseccomp', true),
        Dep('host_libglycin', true),
        Dep('host_gdk_pixbuf', true),
        Dep('host_librsvg', true),
      ],
      default: false,
    )
  end

  def pkg_dirname = "glycin-loaders"

  def expected_files(ver = nil) = [
    ["install/usr/libexec/glycin-loaders/2+/glycin-image-rs", false],
    ["install/usr/libexec/glycin-loaders/2+/glycin-svg", false],
  ]

  def half_flags = [
    "-Dlibglycin=false",
    "-Dglycin-loaders=true",
    "-Dloaders=#{LOADERS.join(",")}",
  ]

  def install_impl_internal(install_dir)
    return build_halves(install_dir)
  end
end

pkgmgr.register(HostLibglycinPackage.new())
pkgmgr.register(HostGlycinLoadersPackage.new())
