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
# host_qemu: the point of the whole hermetic stack.
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
# --disable-werror is not laziness: a 2021 source tree meets a compiler
# from 2024, and GCC 14 turned several long-standing warnings into
# errors by default. If a QEMU version cannot be built by the default
# compiler at all, the answer is to pin it to a contemporary one —
# host_gcc supports majors 11 through 16 for exactly this — rather than
# to patch QEMU.
#
class HostQemuPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  TARGETS = %w[
    i386-softmmu
    x86_64-softmmu
    riscv64-softmmu
  ].freeze

  def initialize
    super(
      name: 'host_qemu',
      source: QEMU_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :hermetic,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [
        Dep('host_gcc', true),
        Dep('host_meson', true),
        Dep('host_glib2', true),
        Dep('host_pixman', true),
        Dep('host_zlib', true),
      ],
      default: false,
    )
  end

  def default_arch = HOST_ARCH
  def default_cc = "syscc"
  def enabled? = HERMETIC_ENABLED

  # Nothing is built against QEMU.
  def sysroot_fragments = []

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

  def install_impl_internal(install_dir)

    prefix = final_install_prefix(install_dir)
    destdir = "#{install_dir}/destdir"
    ok = false

    FileUtils.mkdir_p("build")

    with_hermetic_toolchain do
      chdir("build") do
        ok = run_command("configure.log", [
          "../configure",
          "--prefix=#{prefix}",
          "--target-list=#{TARGETS.join(",")}",

          # Headless for now. The GTK UI is the goal and arrives with
          # its closure; this milestone is about proving the toolchain
          # and the dependency chain underneath it.
          "--disable-gtk",
          "--disable-sdl",
          "--disable-vnc",

          # Each of these would pull in a dependency not built yet.
          "--disable-curses",
          "--disable-libssh",
          "--disable-glusterfs",
          "--disable-seccomp",
          "--disable-capstone",
          "--disable-docs",

          "--disable-werror",
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

    Dir.children(".").each { |e|
      next if e == "install"
      rm_rf(e)
    }
    return true
  end
end

pkgmgr.register(HostQemuPackage.new())
