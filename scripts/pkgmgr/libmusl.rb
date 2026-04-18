# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'arch'
require_relative 'version'
require_relative 'package'
require_relative 'cache'
require_relative 'package_manager'

#
# libmusl — the musl libc source tree, kept in the toolchain as a
# noarch/source-only package. Components that need to consume musl
# headers or grab a specific file from its source (e.g. the elf.h
# copy under include/3rd_party/) can always find it at a stable
# path under TC/noarch/libmusl/<ver>/.
#
# Upstream (git.musl-libc.org) is a plain git repo, not a tarball
# host, so this package overrides `fetch_via_git?` to use the
# git-clone path in the base install_impl flow.
#
LIBMUSL_SOURCE = SourceRef.new(
  name: 'libmusl',
  url:  'https://git.musl-libc.org/git/musl',
  git_tag: ->(ver) { "v#{ver}" },
  fetch_via_git: true,
)

class LibmuslPackage < Package

  include FileShortcuts
  include FileUtilsShortcuts

  def initialize
    super(
      name: 'libmusl',
      source: LIBMUSL_SOURCE,
      on_host: false,
      is_compiler: false,
      arch_list: nil,      # noarch package
      dep_list: []
    )
  end

  def default_arch = nil
  def default_cc = nil

  # pkg_versions uses VER_MUSL (not VER_LIBMUSL) so that gcc.rb and
  # this package agree on a single source of truth for the version.
  def default_ver = pkgmgr.get_config_ver("musl")

  def expected_files = [
    ["Makefile", false],
    ["include", true],
    ["src", true],
  ]

  # Source-only package: nothing to build.
  def install_impl_internal(ignored = nil) = true
end

pkgmgr.register(LibmuslPackage.new())
