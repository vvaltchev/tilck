# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'


class GccCompiler < Package

  attr_reader :target_arch, :libc

  def initialize(target_arch, libc)
    @target_arch = target_arch
    @libc = libc
    super(
      name: "gcc_#{target_arch.name}_#{libc}",
      on_host: true,
      is_compiler: true,
      arch_list: ALL_HOST_ARCHS,
      dep_list: []
    )
  end

  def get_install_list

    def l(s) = s.length
    def drop_prefix(s, prefix) = s[l(prefix)..]
    def drop_suffix(s, suffix) = s[.. -(l(suffix) + 1)]

    list = []
    prefix = "gcc_"
    suffix = "_#{@libc}"
    target_suffix = "_#{@target_arch.name}"

    # The GCC entries look like this:
    #
    #   gcc_13_3_0_riscv64_musl
    #
    for e in HOST_ARCH_DIR_SYS.each_entry do

      e = e.to_s
      next if !e.start_with? prefix
      e = drop_prefix(e, prefix)                  # drop the "gcc_" prefix

      next if !e.end_with? suffix
      e = drop_suffix(e, suffix)                  # drop the "_musl" suffix

      next if !e.end_with? target_suffix
      e = drop_suffix(e, target_suffix)           # drop the _$ARCH suffix

      list.append(InstallInfo.new(nil, true, HOST_ARCH, Ver(e)))
    end

    return list
  end

end

for name, arch in ALL_ARCHS do
  PackageManager.instance.register(
    GccCompiler.new(arch, "musl")
  )
end


