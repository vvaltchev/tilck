# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

LCOV_SOURCE = SourceRef.new(
  name: 'lcov',
  url:  GITHUB + '/linux-test-project/lcov',
)

class LcovPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'lcov',
      source: LCOV_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: nil,      # noarch package
      dep_list: []
    )
  end

  def expected_files(ver = nil) = [
    ["bin", true],
  ]

  def nothing_to_build? = true

  def default_arch = nil
  def default_cc = nil

  # lcov runs on the build host: it is a noarch package (nothing to
  # compile) rather than a target one, so its version comes from the
  # host table even though on_host is false.
  def default_ver = pkgmgr.get_config_ver("lcov", host: true)
end

pkgmgr.register(LcovPackage.new())
