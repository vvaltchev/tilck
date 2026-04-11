# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

class LcovPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'lcov',
      url: GITHUB + '/linux-test-project/lcov',
      on_host: false,
      is_compiler: false,
      arch_list: nil,      # noarch package
      dep_list: []
    )
  end

  def expected_files = [
    ["bin", true],
  ]

  def install_impl_internal(ignored = nil) = true

  def default_arch = nil
  def default_cc = nil
end

pkgmgr.register(LcovPackage.new())
