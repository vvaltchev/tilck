# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

#
# U-Boot — bootloader used by the riscv64 qemu-virt board build of Tilck.
# This Ruby port covers only the qemu-virt board (the default for riscv64);
# the licheerv-nano board uses a vendor build system that's still bash-only.
#
UBOOT_SOURCE = SourceRef.new(
  name: 'uboot',
  url:  'https://ftp.denx.de/pub/u-boot',
  tarname: ->(ver) { "u-boot-#{ver}.tar.bz2" },
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

  # mkimage links OpenSSL. Declared, so the check before the first
  # build says so when it is missing -- and so the recipe may name
  # where the host keeps it.
  def system_deps(ver = nil) = [SystemDeps::OPENSSL]

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

  # In tokens, because it is part of the recipe. On macOS Homebrew's
  # openssl@3 is keg-only -- on no default path -- and $openssl is
  # where the host says it is, resolved when the step runs. The
  # recipe used to run `brew --prefix` right here, which made it a
  # function of the machine rather than of the coordinates, on every
  # staleness check.
  def make_argv
    argv = ["make", "V=1", "-j$PAR"]

    if OS == "Darwin"
      argv += [
        "HOSTCFLAGS=-I$openssl/include",
        "HOSTLDFLAGS=-L$openssl/lib",
      ]
    end

    return argv
  end

end

pkgmgr.register(UbootPackage.new())
