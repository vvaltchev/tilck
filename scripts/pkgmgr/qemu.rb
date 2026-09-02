# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

QEMU_SOURCE = SourceRef.new(
  name: 'qemu',
  url:  'https://download.qemu.org',
  tarname: ->(ver) { "qemu-#{ver}.tar.xz" },
)

#
# host_qemu: the point of the whole host stack.
#
# An APPLICATION rather than a library, which makes it the first
# package of its shape and changes two things:
#
#   * --prefix names its own install directory, not the sysroot. QEMU
#     locates its BIOS blobs and keymaps relative to that prefix at
#     RUNTIME, so it has to be the path it will actually live at.
#     A library is different: nothing looks a library up by prefix, so
#     libraries point theirs at the sysroot and let the farm make it
#     true;
#
#   * it contributes nothing to the sysroot, since nothing is built
#     against it. It is still audited — an application is precisely the
#     thing whose linkage matters — which is why the audit is gated on
#     the tier rather than on contributing a fragment.
#
# The target list is Tilck's architectures. Adding one is a configure
# option, not a dependency, so supporting every system Tilck runs on
# costs nothing beyond build time.
#
# Warning behaviour is left at QEMU's default, deliberately. QEMU
# enables -Werror only for git builds — a release tarball defaults to
# werror off — so nothing here overrides it in either direction. That
# matters: a warning a newer compiler raises on older code is often a
# real undefined behaviour it is about to exploit, and silencing it
# with --disable-werror would hide exactly the signal worth having.
#
# If a QEMU version cannot be built by the default compiler, the answer
# is to pin that build to a contemporary one rather than to quieten the
# compiler.
#
class HostQemuPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  TARGETS = %w[
    i386-softmmu
    x86_64-softmmu
    riscv64-softmmu
  ].freeze

  #
  # WHICH COMPILER EACH QEMU IS BUILT BY.
  #
  # A version of QEMU is built by a compiler from its own time: the
  # one its developers were using, six months to a year older than the
  # release. Building a 2021 QEMU with a 2026 compiler is a test of
  # neither -- new warnings become errors, new optimisations expose
  # old undefined behaviour, and the failures belong to the pairing
  # rather than to either side.
  #
  # Keyed by SERIES, not by exact version, so that a point release we
  # have not listed still gets the right compiler rather than the
  # default one.
  #
  #   QEMU (last of series)   GCC series (first release)      gap
  #   6.2.0   14 Dec 2021     11  (11.1, 27 Apr 2021)      7.6 months
  #   7.2.0   14 Dec 2022     12  (12.1,  6 May 2022)      7.3 months
  #   8.2.0   20 Dec 2023     13  (13.1, 26 Apr 2023)      7.8 months
  #   9.2.0   11 Dec 2024     14  (14.1,  7 May 2024)      7.1 months
  #  10.2.0   24 Dec 2025     15  (15.1, 25 Apr 2025)      8.0 months
  #  11.1.0   11 Aug 2026     16  (16.1, 30 Apr 2026)      3.4 months
  #
  # The last row is the one that does not fit the six-to-twelve month
  # rule, and neither choice does: GCC 15 would be 15.6 months older
  # than QEMU 11.1. GCC 16 is the compiler that existed while QEMU 11
  # was being written -- 16.1 landed a week after 11.0 opened the
  # series -- so it is the contemporary one even though the gap is
  # short.
  #
  GCC_FOR = {
    6  => Ver("11.5.0"),
    7  => Ver("12.5.0"),
    8  => Ver("13.4.0"),
    9  => Ver("14.4.0"),
    10 => Ver("15.3.0"),
    11 => Ver("16.2.0"),
  }.freeze

  # One point release per series: the last of each, which carries that
  # series' fixes. Naming another version still works -- the table is
  # keyed by series -- but these are the ones this tree is about.
  SUPPORTED = [
    Ver("6.2.0"), Ver("7.2.0"), Ver("8.2.0"),
    Ver("9.2.0"), Ver("10.2.0"), Ver("11.1.0"),
  ].freeze

  def initialize
    super(
      name: 'host_qemu',
      source: QEMU_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :stack,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_glib2', true),
        Dep('host_pixman', true),
        Dep('host_zlib', true),
        Dep('host_gtk3', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def installable_versions = SUPPORTED

  # The compiler this version of QEMU is built by, pinned rather than
  # left to HOST_VER_GCC. Asking for a QEMU therefore asks for its
  # stack, and `-s host_qemu:9.2.0` builds gcc 14.4.0, its glibc and
  # the whole GTK closure underneath -- resolve_install_plan reads the
  # host_gcc version out of this and installs into that stack.
  def dep_list_for(ver = nil)

    gcc = GCC_FOR[(ver || default_ver).series]
    return dep_list if gcc.nil?

    # Replace rather than append: the same package named twice, once
    # bare and once pinned, leaves which one wins to the order the
    # solver happens to walk them in.
    base = dep_list.reject { |d| d.name == "host_gcc" }
    return base + [Dep('host_gcc', true, ver: gcc)]
  end

  # Nothing is built against QEMU.
  def sysroot_fragments(gcc_ver = nil) = []

  def expected_files(ver = nil) = [
    ["install/bin/qemu-system-i386", false],
    ["install/bin/qemu-system-x86_64", false],
    ["install/bin/qemu-system-riscv64", false],
  ]

  def clean_build(dir)
    FileUtils.rm_rf(dir / "install")
    FileUtils.rm_rf(dir / "build")
    super(dir)
  end

  # QEMU stops dead on a configure option it does not recognise --
  #
  #   ERROR: unknown option --disable-glusterfs
  #
  # -- so an option that upstream removes has to stop being passed to
  # the versions that no longer have it. glusterfs is the first: it is
  # a feature in 10.2's and 11.0's meson_options.txt and gone from
  # 11.1's, so the boundary falls inside series 11 and a table keyed
  # by series could not express it.
  #
  # Everything else we pass is still an option in 11.1.
  GLUSTERFS_DROPPED = Ver("11.1.0")

  def configure_flags(ver)

    flags = [
      "--target-list=#{TARGETS.join(",")}",

      # The GTK UI, which is what the whole closure below this package
      # exists for. QEMU 6.2's ui/gtk.c targets GTK 3, and 11.1's
      # still does.
      "--enable-gtk",

      # VNC's core needs nothing we do not already have; the optional
      # encoders it can use (JPEG, PNG-over-VNC, SASL) stay out.
      "--enable-vnc",

      # SDL would be a second UI toolkit for the same job, and curses
      # would need an ncurses inside the sysroot -- the one in the
      # tree is a host-tier build for menuconfig, not part of this
      # stack.
      "--disable-sdl",
      "--disable-curses",

      # Each of these would pull in a dependency not built yet, or
      # find one on the build machine and link it.
      "--disable-libssh",
      "--disable-seccomp",
      "--disable-capstone",
      "--disable-docs",

      # xkbcommon is a BUILD-TIME dependency here, not a runtime one:
      # qemu-system links nothing from it, only the qemu-keymap tool
      # does. Its whole effect is that meson REGENERATES the keymaps
      # in pc-bios/keymaps instead of installing the ones the tarball
      # ships, and regenerating them needs xkeyboard-config's data --
      # rules/evdev and the rest -- which this tree has no reason to
      # carry:
      #
      #   qemu-keymap -f pc-bios/keymaps/cz -l cz
      #   xkbcommon: ERROR: [XKB-632] Failed to add any default
      #     include path (system path: .../sysroot/usr/share/X11/xkb)
      #
      # The shipped keymaps are what upstream generated from that same
      # data, so installing them is the same answer without the
      # package. QEMU 7.2 already did exactly this -- its installed
      # keymaps carry the tarball's timestamp -- but by accident,
      # because meson did not find the library. Saying so makes every
      # version build the same way instead of depending on what a
      # search happened to turn up.
      "--disable-xkbcommon",
    ]

    flags << "--disable-glusterfs" if ver < GLUSTERFS_DROPPED
    return flags
  end

  def install_impl_internal(install_dir)

    prefix = final_install_prefix(install_dir)
    destdir = "#{install_dir}/destdir"
    ver = installing_ver(install_dir)
    ok = false

    FileUtils.mkdir_p("build")

    with_stack_toolchain do
      chdir("build") do
        ok = run_command("configure.log", [
          "../configure",
          "--prefix=#{prefix}",
          *configure_flags(ver),
        ])
        next if !ok

        ok = run_command("build.log", ["ninja"])
        next if !ok

        # Through DESTDIR, so the tree handed to the atomic move is
        # complete while the paths inside it name where it is going.
        # QEMU's build directory is meson-generated, so its installer
        # is the one to ask.
        ok = run_command("install.log",
                         ["meson", "install", "--destdir=#{destdir}"])
      end
    end

    return false if !ok

    FileUtils.mv("#{destdir}#{prefix}", "#{install_dir}/install")

    prune_build_tree
    return true
  end
end

pkgmgr.register(HostQemuPackage.new())
