# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'
require_relative 'system_deps'

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
# host_glycin: sandboxed image loading, and the one Rust package in
# the tree.
#
# gdk-pixbuf 2.44 asks for it by pkg-config — `glycin-2 >=
# 2.2.alpha.7` — rather than bundling it, so it is a package like any
# other here. Only prereleases of 2.2 exist upstream; that is what the
# requirement names and there is no final release to wait for.
#
# glycin does not decode images in-process. Each format is handled by
# a separate process confined with seccomp, on the reasoning that an
# image parser is where untrusted input meets C. That is why
# libseccomp is a dependency, and why the loaders are built here too:
# libglycin without them links and then decodes nothing.
#
# Of the four loaders upstream builds by default, only glycin-image-rs
# is built. It is pure Rust and covers PNG, JPEG, GIF and WebP — what
# a GTK icon theme is made of. The other three would each drag a codec
# stack in behind them: glycin-heif wants libheif, glycin-jxl wants
# libjxl, and glycin-svg wants librsvg AND gtk4.
#
# DECLARED PORTABLE, and getting there is the interesting part.
#
# Rust's default is to link with the system cc, which would produce a
# library against the system glibc. That is not merely untidy: a
# library linked on a host whose glibc is NEWER than ours cannot run
# against ours, so the build would work on this machine and fail on
# somebody else's. It would also drag gdk-pixbuf, GTK and QEMU out of
# the portable tree behind it, since a portable package cannot rest on
# a distro-specific one.
#
# See write_cross_file and the RUSTFLAGS in install_impl_internal for
# how the linker is redirected without breaking the proc-macros. The
# result, measured under a hostile LD_LIBRARY_PATH:
#
#   libc.so.6       => <sysroot>/usr/lib/libc.so.6
#   libseccomp.so.2 => <sysroot>/usr/lib/libseccomp.so.2
#   libglib-2.0.so.0 => <sysroot>/usr/lib/libglib-2.0.so.0
#
# with nothing resolving outside toolchain4. The audit at the end of
# the install is what keeps it that way.
#
class HostGlycinPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  # The host triple cargo builds for. Only x86_64 Linux is supported
  # as a build host for this today; when that changes, this has to be
  # derived from HOST_ARCH rather than written down.
  CARGO_TRIPLE = "x86_64-unknown-linux-gnu"

  def initialize
    super(
      name: 'host_glycin',
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

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HOST_STACK_ENABLED
  def pkg_dirname = "glycin"

  # Rust is not built from source: rustup gives a better toolchain
  # than we would, and a compiler is a large thing to build for one
  # image loader. Declared instead, so a machine without it is told
  # before the build starts rather than by cargo forty minutes in.
  def system_deps(ver = nil)
    return [SystemDeps::RUSTC, SystemDeps::CARGO]
  end

  def expected_files(ver = nil) = [
    ["install/usr/lib/libglycin-2.so", false],
    ["install/usr/lib/pkgconfig/glycin-2.pc", false],
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

  #
  # A meson cross file, for a build that is not cross-compiling.
  #
  # It is here for one line of glycin's meson.build:
  #
  #   if meson.is_cross_build()
  #     cargo_target_args = ['--target', <rust_target>]
  #
  # --target is what separates HOST units from TARGET units, and that
  # distinction is the whole difficulty here.
  #
  # Proc-macros are compiled for the host and then dlopen'd by rustc
  # itself. Link one against our glibc and it is loaded into a system
  # rustc running under the system glibc -- two libcs in one process
  # -- and the build dies with `error: cannot determine resolution for
  # the import` (E0463) out of zvariant, which says nothing whatever
  # about the real cause. That is exactly what
  # CARGO_TARGET_<triple>_LINKER does: measured here, it applies to
  # host units too, --target or not.
  #
  # RUSTFLAGS does not: with --target given, cargo passes it only to
  # the target units. So the linker is set THERE (see
  # install_impl_internal) rather than through the cargo variable, and
  # proc-macros keep the system cc while the library we ship links
  # with ours.
  #
  # needs_exe_wrapper=false because the "cross" target is this very
  # machine and meson may run what it builds.
  #
  def write_cross_file(gcc_bin, bu_bin, rustc)

    path = File.expand_path("tilck-cross.ini")

    File.write(path, <<~CROSS)
      [binaries]
      c = '#{gcc_bin}/gcc'
      cpp = '#{gcc_bin}/g++'
      ar = '#{bu_bin}/ar'
      strip = '#{bu_bin}/strip'
      pkg-config = 'pkg-config'
      rust = '#{rustc}'

      [properties]
      needs_exe_wrapper = false
      rust_target = '#{CARGO_TRIPLE}'

      [host_machine]
      system = 'linux'
      cpu_family = 'x86_64'
      cpu = 'x86_64'
      endian = 'little'
    CROSS

    return path
  end

  # The 2.2 prerelease tarballs ship no po/ directory at all, while
  # glycin-loaders/meson.build unconditionally merges translations
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

  def install_impl_internal(install_dir)

    gcc_bin, bu_bin = stack_toolchain_bins
    env = SystemDeps::Env.new
    cargo = env.which("cargo")
    rustc = env.which("rustc")

    if cargo.nil? || rustc.nil?
      error "#{name}: cargo and rustc must both be on the host"
      return false
    end

    ensure_po_dir
    cross = write_cross_file(gcc_bin, bu_bin, rustc)

    # cargo and rustc are found through PATH by meson. Set outside
    # with_stack_toolchain (which meson_stack_build enters):
    # that method rebuilds PATH from ENV["PATH"], so what is prepended
    # here survives, behind our own compiler rather than ahead of it.
    with_saved_env(["PATH", "RUSTFLAGS"]) do

      ENV["PATH"] = "#{File.dirname(cargo)}:#{ENV["PATH"]}"
      ENV["RUSTFLAGS"] = "-C linker=#{gcc_bin}/gcc"

      meson_stack_build(install_dir, args: [
        "--cross-file=#{cross}",
        "-Dlibglycin=true",
        "-Dglycin-loaders=true",
        "-Dloaders=glycin-image-rs",
        "-Dlibglycin-gtk4=false",
        "-Dglycin-thumbnailer=false",
        "-Dintrospection=false",
        "-Dvapi=false",
        "-Dtests=false",
        "-Dpython_tests=false",
      ])
    end
  end
end

pkgmgr.register(HostGlycinPackage.new())
