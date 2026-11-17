# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

#
# U-Boot — bootloader used by the riscv64 qemu-virt board build of Tilck.
# The licheerv-nano board boots from its vendor BSP instead, which has a
# u-boot of its own (licheerv_nano_boot).
#
# Cloned from the mirror U-Boot keeps on github (doc/build/source.rst),
# where its releases are the v<year>.<month> tags, and pinned by commit.
# The release tarballs on ftp.denx.de were the source until that host
# stopped answering, in November 2026.
#
UBOOT_SOURCE = SourceRef.new(
  name: 'uboot',
  url:  GITHUB + '/u-boot/u-boot',
  git_tag: ->(ver) { "v#{ver}" },
)

class UbootPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'uboot',
      source: UBOOT_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: { "riscv64" => ALL_ARCHS["riscv64"] },
      dep_list: [Dep('host_ncurses', true)],
      default: true,
      board_list: ["qemu-virt"],
    )
  end

  def expected_files(ver = nil) = [
    ["u-boot.bin", false],
    ["tools/mkimage", false],
  ]

  def uboot_config = board_bsp / "u-boot.config"

  def build_steps(ver = default_ver) = [
    Copy(from: src_path(uboot_config), to: ".config"),
    Run(log: "build.log", argv: make_argv),
  ]

  def configurable? = true

  def config_impl
    # configure runs this in the installed version's directory.
    ctx = BuildCtx.new(self, Pathname.pwd)
    be = deps_build_env.expand(ctx)

    ok = system(be.env, "make", *be.kconfig_make_vars, "menuconfig")
    return false if !ok

    fix_config_file

    print "Update #{uboot_config.basename} with the new config? [Y/n]: "
    answer = STDIN.gets&.strip&.downcase

    if answer.nil? || answer.empty? || answer == "y"
      cp ".config", uboot_config.to_s
      info "Source file #{uboot_config} UPDATED"
    end

    # Rebuild with the new configuration: the recipe's own make line,
    # its tokens resolved here since this is not a recipe run.
    # ncurses is only needed for menuconfig itself.
    info "Rebuilding #{name}..."
    ok = run_command("build.log", ctx.expand_all(make_argv))
    return false if !ok

    return true
  end

  private

  # The recipe's make line, in tokens, and the one -C rebuilds with.
  #
  # The host tools are built without OpenSSL (CONFIG_TOOLS_LIBCRYPTO
  # off in u-boot.config): libcrypto is only for signing images, which
  # Tilck's FIT does not do, and OpenSSL 4 removed the ENGINE API that
  # u-boot's signing code calls -- no u-boot release builds against it
  # (2026.10 does not). Arch ships OpenSSL 4 already, and Fedora 45
  # no longer has the openssl-devel-engine package it needed.
  def make_argv = ["make", "V=1", "-j$PAR"]

end

pkgmgr.register(UbootPackage.new())
