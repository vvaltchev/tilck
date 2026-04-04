# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

X86_ARCHS = ALL_ARCHS.select { |_, a| a.family == "generic_x86" }

GNUEFI_URL = GITHUB + '/vvaltchev/gnu-efi-fork'

GNUEFI_PATCHES = {
  'typedef wchar_t CHAR16' =>
    'typedef unsigned short CHAR16',

  'typedef uint8_t                 BOOLEAN;' =>
    'typedef char       CHAR8;',
}

GNUEFI_COMMON_EXPECTED_FILES = [
  ["inc", true],
  ["gnuefi", true],
  ["lib", true],
  ["Makefile", false],
]

def gnuefi_apply_patches
  for efi_arch in ["ia32", "x86_64", "riscv64"]
    file = "inc/#{efi_arch}/efibind.h"
    next if !File.exist?(file)
    s = File.read(file)
    GNUEFI_PATCHES.each { |before, after| s = s.gsub(before, after) }
    File.write(file, s)
  end
end

#
# Source-only (noarch) gnuefi: just the extracted source tree.
# Used by kernel C files to include GNU-EFI headers.
#
class GnuefiSrcPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'gnuefi_src',
      url: GNUEFI_URL,
      on_host: false,
      is_compiler: false,
      arch_list: nil,      # noarch
      dep_list: []
    )
  end

  def pkg_dirname = "gnuefi"
  def tarname(ver) = "gnuefi-#{ver}.tgz"
  def default_ver = pkgmgr.get_config_ver("gnuefi")
  def expected_files = GNUEFI_COMMON_EXPECTED_FILES
  def default_arch = nil
  def default_cc = nil

  def install_impl_internal(ignored = nil)
    return true
  end
end

#
# Arch-specific gnuefi: patched and compiled for x86 targets.
# Depends on gnuefi_src (shares the same tarball in cache).
#
class GnuefiPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'gnuefi',
      url: GNUEFI_URL,
      on_host: false,
      is_compiler: false,
      arch_list: X86_ARCHS,
      dep_list: [Dep('gnuefi_src', false)]
    )
  end

  def pkg_dirname = "gnuefi"

  def expected_files = GNUEFI_COMMON_EXPECTED_FILES

  #
  # The UEFI bootloader always needs x86_64 gnuefi, even when the
  # target arch is i386. Mirror the bash script behavior: build for
  # the current arch + x86_64.
  #
  def archs_needed
    archs = [ARCH]
    x64 = ALL_ARCHS["x86_64"]
    archs << x64 if ARCH != x64
    archs
  end

  def installed?(ver)
    list = get_install_list()
    archs_needed.all? do |arch|
      list.any? { |x| x.ver == ver && x.arch == arch && !x.broken }
    end
  end

  def install_impl(ver)

    info "Install #{name} version: #{ver}"

    if installed?(ver)
      info "Package already installed, skip"
      return nil
    end

    ok = Cache::download_git_repo(url, tarname(ver), git_tag(ver))
    return false if !ok

    for arch in archs_needed
      pkgmgr.with_cc(arch.name) do |arch_dir|
        chdir_package_base_dir(arch_dir) do
          ok = Cache::extract_file(tarname(ver), ver_dirname(ver))
          return false if !ok
          ok = chdir_install_dir(arch_dir, ver) do
            d = mkpathname(getwd)
            ok = install_impl_internal(d, arch)
            ok = check_install_dir(d, true) if ok
          end
        end
      end
      return false if !ok
    end

    return ok
  end

  def install_impl_internal(install_dir, arch = nil)

    arch ||= default_arch()
    gnuefi_apply_patches()

    efi = arch.efi
    tc = arch.gcc_tc

    ok = run_command("build_#{efi}.log", [
      "make",
      "ARCH=#{efi}",
      "prefix=#{tc}-linux-",
      "CROSS_COMPILE=",
      "-j#{BUILD_PAR}",
    ])
    return false if !ok
    return true
  end
end

pkgmgr.register(GnuefiSrcPackage.new())
pkgmgr.register(GnuefiPackage.new())
