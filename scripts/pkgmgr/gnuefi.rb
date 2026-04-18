# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

GNUEFI_URL = GITHUB + '/vvaltchev/gnu-efi-fork'

#
# Shared SourceRef: `gnuefi_src` (headers consumed by kernel build)
# and `gnuefi` (arch-specific built libraries) both fetch from the
# same upstream and so share a single SourceRef — the tarball is
# downloaded once and cached.
#
GNUEFI_SOURCE = SourceRef.new(
  name: 'gnuefi',
  url:  GNUEFI_URL,
)

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
# Used by kernel C files to include GNU-EFI headers — a legitimate
# deliverable, not an implementation detail. Needed on every arch
# including those where the built libraries aren't produced.
#
class GnuefiSourcePackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'gnuefi_src',
      source: GNUEFI_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: nil,      # noarch
      dep_list: [],
      default: true,
    )
  end

  def pkg_dirname = "gnuefi"
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
      source: GNUEFI_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: X86_ARCHS.values,
      dep_list: [Dep('gnuefi_src', false)],
      default: true,
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
    # Use default_arch (not the bare ARCH) so that -s gnuefi -a <x86>
    # still computes the pair correctly: target arch + x86_64.
    archs = [default_arch]
    x64 = ALL_ARCHS["x86_64"]
    archs << x64 if default_arch != x64
    archs
  end

  def installed?(ver)
    list = get_install_list()
    archs_needed.all? do |arch|
      list.any? { |x|
        x.ver == ver && x.arch == arch &&
        x.compiler == arch.gcc_ver && !x.broken
      }
    end
  end

  def install_impl(ver)

    info "Install #{name} version: #{ver}"

    if installed?(ver)
      info "Package already installed, skip"
      return nil
    end

    ok = @source.download(ver)
    return false if !ok

    for arch in archs_needed
      pkgmgr.with_cc(arch.name) do |arch_dir|
        chdir_package_base_dir(arch_dir) do
          ok = @source.extract(ver, ver_dirname(ver))
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
      "OS=Linux",
      "-j#{BUILD_PAR}",
    ])
    return false if !ok
    return true
  end
end

pkgmgr.register(GnuefiSourcePackage.new())
pkgmgr.register(GnuefiPackage.new())
