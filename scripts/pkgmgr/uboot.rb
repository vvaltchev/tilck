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
      dep_list: [],
      default: true,
      board_list: ["qemu-virt"],
    )
  end

  def expected_files = [
    ["u-boot.bin", false],
    ["tools/mkimage", false],
  ]

  def uboot_config = BOARD_BSP / "u-boot.config"

  def install_impl_internal(install_dir)
    patch_qemu_riscv_scriptaddr
    cp uboot_config, ".config"

    ok = run_command("build.log", make_argv)
    return ok
  end

  def configurable? = true

  def config_impl
    ok = system("make", "menuconfig")
    return false if !ok

    fix_config_file

    print "Update #{uboot_config.basename} with the new config? [Y/n]: "
    answer = STDIN.gets&.strip&.downcase

    if answer.nil? || answer.empty? || answer == "y"
      cp ".config", uboot_config.to_s
      info "Source file #{uboot_config} UPDATED"
    end

    # Rebuild with the new configuration
    info "Rebuilding #{name}..."
    ok = run_command("build.log", make_argv)
    return false if !ok

    return true
  end

  private

  def make_argv
    argv = [ "make", "V=1", "-j#{BUILD_PAR}" ]

    if OS == "Darwin"
      ssl = `brew --prefix openssl@3`.strip
      if !ssl.empty? && File.directory?(ssl)
        argv += [
          "HOSTCFLAGS=-I#{ssl}/include",
          "HOSTLDFLAGS=-L#{ssl}/lib",
        ]
      end
    end

    return argv
  end

  #
  # The default scriptaddr in qemu-riscv.h (0x8c100000) sits above the top
  # of RAM when QEMU is launched with the 128 MB default, so u-boot fails
  # to load boot.scr. Move the address into the low 128 MB range to match
  # what the bash bootloader script does.
  #
  def patch_qemu_riscv_scriptaddr
    file = "include/configs/qemu-riscv.h"
    if !File.exist?(file)
      raise LocalError, "uboot: expected file not found: #{file}"
    end
    data = File.read(file)
    data.gsub!("scriptaddr=0x8c100000", "scriptaddr=0x80200000")
    File.write(file, data)
  end
end

pkgmgr.register(UbootPackage.new())
