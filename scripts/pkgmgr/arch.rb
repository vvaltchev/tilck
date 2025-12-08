# SPDX-License-Identifier: BSD-2-Clause

require_relative 'version_check'
require 'etc'

DEFAULT_ARCH = "i386"

Architecture = Struct.new(

  # Type name
  "Architecture",

  # Const fields
  :name,                # Tilck name
  :family,              # Family name e.g. generic_x86
  :elf_name,            # ELF arch name used for various tools
  :ld_output,           # Output format for linker scripts
  :efi,                 # UEFI architecture name
  :gcc_tc,              # Arch name for GCC toolchain
  :boards,              # List of boards (only for embedded architectures)
  :default_board,

  # Determined at runtime
  :min_gcc_ver,
  :default_gcc_ver,
  :gcc_ver,
  :target_dir,
  :host_dir,
  :host_syscc_dir,
) do

  # To string conversion
  def to_s = name

  # Comparison operator
  def ==(other)
    case other
      when String then name == other
    else
      super # Struct's default: same class + member-wise equality
    end
  end

  # For hash maps
  def eql?(other) = (self == other)
  def hash = name.hash
end

ALL_ARCHS = [
  Architecture.new(
    name: "i386",
    family: "generic_x86",
    elf_name: "elf32-i386",
    ld_output: "elf_i386",
    efi: "ia32",
    gcc_tc: "i686",
  ),
  Architecture.new(
    name: "x86_64",
    family: "generic_x86",
    elf_name: "elf64-x86-64",
    ld_output: "elf_x86_64",
    efi: "x86_64",
    gcc_tc: "x86_64",
  ),
  Architecture.new(
    name: "riscv64",
    family: "riscv",
    elf_name: "elf64-littleriscv",
    ld_output: "elf64lriscv",
    efi: nil,
    gcc_tc: "riscv64",
    boards: ["qemu-virt", "licheerv-nano"],
    default_board: "qemu-virt",
  ),
  Architecture.new(
    name: "aarch64",
    family: "arm",
    elf_name: "elf64-littleaarch64",
    ld_output: "aarch64elf",
    efi: nil,
    gcc_tc: "aarch64",
  ),
].to_h { |a| [ a.name, a ] }

ALL_HOST_ARCHS = [
  "x86_64",
  "aarch64",
].to_h { |a| [ a, ALL_ARCHS[a] ] }


