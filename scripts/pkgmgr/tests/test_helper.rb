# SPDX-License-Identifier: BSD-2-Clause

require 'minitest/autorun'
require 'tmpdir'
require 'fileutils'
require 'pathname'

# Load the pkgmgr modules (this sets up all global constants).
# Tests that need different constant values use `with_context`.
require_relative '../early_logic'
require_relative '../arch'
require_relative '../version'
require_relative '../package'
require_relative '../dep_resolver'
require_relative '../cache'
require_relative '../package_manager'

module TestHelper

  # Temporarily override top-level constants for the duration of a block.
  # Example: with_context(ARCH: ALL_ARCHS["riscv64"], BOARD: "qemu-virt")
  def with_context(**overrides)
    saved = {}
    old_verbose = $VERBOSE
    $VERBOSE = nil  # suppress "already initialized constant" warnings

    overrides.each do |name, value|
      saved[name] = Object.const_get(name)
      Object.send(:remove_const, name)
      Object.const_set(name, value)
    end

    $VERBOSE = old_verbose
    yield
  ensure
    $VERBOSE = nil
    saved.each do |name, value|
      Object.send(:remove_const, name) if Object.const_defined?(name)
      Object.const_set(name, value)
    end
    $VERBOSE = old_verbose
  end

  # Create a temporary toolchain directory tree. Returns a Pathname.
  # Cleans up automatically when the block returns.
  def with_fake_tc
    Dir.mktmpdir("pkgmgr-test-") do |dir|
      tc = Pathname.new(dir)
      FileUtils.mkdir_p(tc / "cache")
      FileUtils.mkdir_p(tc / "noarch")
      yield tc
    end
  end

  # Reset the PackageManager singleton, clearing all registered packages
  # and cached state.
  def reset_pkgmgr!
    pm = PackageManager.instance
    pm.instance_variable_set(:@packages, {})
    pm.instance_variable_set(:@known_pkgs_paths, nil)
    pm.instance_variable_set(:@known_installed, nil)
    pm.instance_variable_set(:@found_installed, nil)
    pm.instance_variable_set(:@installable, nil)
  end

  # A minimal Package subclass for testing. Downloads, builds, and
  # filesystem operations are replaced with in-memory tracking.
  #
  # Usage:
  #   pkg = FakePackage.new("foo", dep_list: [Dep("bar", false)])
  #   pkgmgr.register(pkg)
  #
  class FakePackage < Package

    # Class-level install log shared across all FakePackage instances.
    # Tests can read this to verify install order.
    @@install_log = []
    def self.install_log = @@install_log
    def self.clear_log! = @@install_log.clear

    # Set of package names that should fail on install (for negative tests).
    @@fail_set = Set.new
    def self.fail_set = @@fail_set
    def self.clear_fail_set! = @@fail_set.clear

    attr_reader :fake_installed_versions

    def initialize(name, dep_list: [], arch_list: ALL_ARCHS,
                   on_host: false, is_compiler: false,
                   default: false, board_list: nil,
                   host_os_list: nil, host_arch_list: nil,
                   portable: false)
      super(
        name: name,
        url: "https://fake/#{name}",
        on_host: on_host,
        is_compiler: is_compiler,
        portable: portable,
        arch_list: arch_list,
        dep_list: dep_list,
        host_os_list: host_os_list,
        host_arch_list: host_arch_list,
        default: default,
        board_list: board_list,
      )
      @fake_installed_versions = Set.new
    end

    def expected_files = []
    def tarname(ver) = "#{name}-#{ver}.tgz"

    # Override install_impl to skip all filesystem/network operations.
    def install_impl(ver)
      if !host_supported?
        return false
      end
      if !board_supported?
        return false
      end

      ver ||= default_ver
      if @fake_installed_versions.include?(ver)
        return nil  # already installed
      end

      if @@fail_set.include?(name)
        return false
      end

      @@install_log << name
      @fake_installed_versions.add(ver)
      return true
    end

    # Override get_install_list to return from our in-memory state.
    def get_install_list
      @fake_installed_versions.map { |ver|
        InstallInfo.new(
          name, default_cc, on_host, default_arch, ver,
          Pathname.new("/fake/#{name}/#{ver}"), self, false
        )
      }
    end

    def installed?(ver)
      @fake_installed_versions.include?(ver)
    end
  end
end
