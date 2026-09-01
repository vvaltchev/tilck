# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

ACPICA_SOURCE = SourceRef.new(
  name: 'acpica',
  url:  GITHUB + '/acpica/acpica',
)

class Acpica < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'acpica',
      source: ACPICA_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: nil,      # nil => noarch package
      dep_list: [],
      default: true,
    )
  end

  def expected_files(ver = nil) = [
    ["3rd_party", true],
    ["Makefile", false],
    ["source", true],
    ["source/components/namespace", true],
  ]

  # The source edit this build used to make in place is a patch now,
  # applied and fingerprinted by the base class. What is left is not a
  # build at all: acpica ships sources the kernel compiles itself, and
  # the install only has to leave them where its include path expects.
  def install_impl_internal(ignored = nil)
    chdir!("3rd_party") {
      File.write("README", "Directory created by Tilck")
      ln_s("../source/include", "acpi")
    }
    return true
  end

  def default_arch = nil
  def default_cc = nil
end

pkgmgr.register(Acpica.new())
