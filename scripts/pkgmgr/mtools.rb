# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

MTOOLS_SOURCE = SourceRef.new(
  name: 'mtools',
  url:  'https://ftp.gnu.org/gnu/mtools',
  tarname: ->(ver) { "mtools-#{ver}.tar.gz" },
)

class MtoolsPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'host_mtools',
      source: MTOOLS_SOURCE,
      on_host: true,
      is_compiler: false,
      host_tier: :distro,
      arch_list: ALL_HOST_ARCHS.values,
      dep_list: [],
      default: true,
    )
  end

  def default_arch = HOST_ARCH

  def expected_files(ver = nil) = [
    "mtools"
  ]

  def build_steps

    conf_params = [
      "--without-x",
    ]

    if OS == "FreeBSD" or OS == "Darwin"
      conf_params << "LIBS=-liconv"
    end

    return [
      Step("configure.log", ["./configure", *conf_params]),
      Step("build.log", ["make", "-j$PAR"]),
    ]
  end
end

pkgmgr.register(MtoolsPackage.new())
