# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

#
# LicheeRV Nano boot — vendor BSP that builds the FSBL + u-boot bundle
# (`fip.bin`) for the Sipeed LicheeRV Nano board (sg2002 SoC). Pulled
# straight from the upstream tag archive on GitHub.
#
# This build is Linux-only because:
#   1. it depends on host_sophgo_tools, which ships x86_64 Linux ELF
#      cross-binaries (no macOS / FreeBSD builds upstream);
#   2. cvisetup.sh + the underlying makefiles are only validated on
#      Ubuntu by upstream.
#
# The actual compile is driven by the vendor's build/cvisetup.sh, which
# defines bash functions (`defconfig`, `build_uboot`, ...) and exports
# its own toolchain. We must therefore *clear* CC/CXX/AR/NM/RANLIB and
# CROSS_PREFIX/CROSS_COMPILE before sourcing it — otherwise the values
# inherited from with_cc would override the vendor's choices and break
# the build.
#
#
# Upstream (sipeed) serves the tarball as `<tag>.tar.gz`; store it in
# the cache under a qualified name so it doesn't collide with other
# GitHub tag archives that share the same bare version number.
#
LICHEERV_NANO_BOOT_SOURCE = SourceRef.new(
  name: 'licheerv_nano_boot',
  url:  GITHUB + '/sipeed/LicheeRV-Nano-Build/archive/refs/tags',
  tarname:        ->(ver) { "licheerv_nano_boot-#{ver}.tar.gz" },
  remote_tarname: ->(ver) { "#{ver}.tar.gz" },
)

class LicheervNanoBootPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  CODENAME = "sg2002_licheervnano_sd"

  def initialize
    super(
      name: 'licheerv_nano_boot',
      source: LICHEERV_NANO_BOOT_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: Archs("riscv64"),
      dep_list: [Dep('host_sophgo_tools', true)],
      host_os_list: ["linux"],
      host_arch_list: ["x86_64"],
      default: true,
      board_list: ["licheerv-nano"],
    )
  end

  def expected_files(ver = nil) = [
    ["build/cvisetup.sh", false],
    ["install/soc_#{CODENAME}/fip.bin", false],
  ]

  def install_impl_internal(install_dir)

    # Symlink sophgo's prebuilt host tools as `host-tools/` inside the
    # licheerv source tree — that's the path the vendor build system
    # (cvisetup.sh + the u-boot makefiles) hardcodes.
    sophgo_dir = sophgo_tools_install_dir
    if sophgo_dir.nil? || !sophgo_dir.directory?
      error "host_sophgo_tools install dir not found: #{sophgo_dir}"
      return false
    end
    rm_f("host-tools")
    ln_s(sophgo_dir.to_s, "host-tools")

    # Run the vendor build with our cross-compiler env *unset* so that
    # cvisetup.sh's own toolchain selection wins. with_saved_env will
    # restore the prior values when we leave the block.
    with_saved_env(
      %w[CC CXX AR NM RANLIB CROSS_PREFIX CROSS_COMPILE]
    ) do
      ENV["CC"]            = ""
      ENV["CXX"]           = ""
      ENV["AR"]            = ""
      ENV["NM"]            = ""
      ENV["RANLIB"]        = ""
      ENV["CROSS_PREFIX"]  = ""
      ENV["CROSS_COMPILE"] = ""

      ok = run_command("build.log", [
        "bash", "-c",
        "source build/cvisetup.sh && " +
        "defconfig #{CODENAME} && " +
        "build_uboot"
      ])
      return false if !ok
    end

    return true
  end

  private

  # Locate the installed host_sophgo_tools tree. It is a host-side,
  # portable package (see sophgo_tools.rb), so all installs are on
  # HOST_ARCH with compiler == "syscc" — no target-arch filtering.
  def sophgo_tools_install_dir
    pkg = pkgmgr.get("host_sophgo_tools")
    return nil if pkg.nil?
    list = pkg.get_install_list.select { |x| !x.broken }
    return nil if list.empty?
    return mkpathname(list.first.path.to_s)
  end

end

pkgmgr.register(LicheervNanoBootPackage.new())
