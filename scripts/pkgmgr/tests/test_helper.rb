# SPDX-License-Identifier: BSD-2-Clause

require 'minitest/autorun'
require 'tmpdir'
require 'fileutils'
require 'pathname'
require 'set'

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
    $VERBOSE = nil

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

  # Reset the PackageManager singleton, clearing all registered packages
  # and cached state. Also reads config versions fresh.
  def reset_pkgmgr!
    pm = PackageManager.instance
    pm.instance_variable_set(:@packages, {})
    pm.instance_variable_set(:@known_pkgs_paths, nil)
    pm.instance_variable_set(:@known_installed, [])
    pm.instance_variable_set(:@found_installed, [])
    pm.instance_variable_set(:@installable, [])
  end

  # Create a temp toolchain directory tree and run the block with TC
  # and related constants pointing at it. Cleans up on exit.
  # Fixed architecture and GCC version for all tests, so results are
  # deterministic regardless of the user's ARCH= environment variable.
  FAKE_ARCH = ALL_ARCHS["i386"]
  FAKE_GCC_VER = Ver("13.3.0")

  def with_fake_tc
    Dir.mktmpdir("pkgmgr-test-") do |dir|
      tc = Pathname.new(dir)
      FileUtils.mkdir_p(tc / "cache")
      FileUtils.mkdir_p(tc / "staging")
      FileUtils.mkdir_p(tc / "noarch")
      FileUtils.mkdir_p(tc / "gcc-#{FAKE_GCC_VER}" / FAKE_ARCH.name)

      # Set gcc_ver for all architectures (normally done by main.rb's
      # read_gcc_ver_defaults, which tests don't call).
      saved_gcc_vers = ALL_ARCHS.map { |name, arch| [name, arch.gcc_ver] }
      ALL_ARCHS.each_value { |arch| arch.gcc_ver = FAKE_GCC_VER }

      host_dir_p = tc / "host" / "#{HOST_OS}-#{HOST_ARCH.name}" / "portable"
      host_dir_d = tc / "host" / "#{HOST_OS}-#{HOST_ARCH.name}" / HOST_DISTRO
      host_dir   = host_dir_d / HOST_CC
      FileUtils.mkdir_p(host_dir_p)
      FileUtils.mkdir_p(host_dir)

      with_context(
        ARCH: FAKE_ARCH,
        BOARD: nil,
        DEFAULT_BOARD: nil,
        TC: tc,
        TC_CACHE: tc / "cache",
        TC_STAGING: tc / "staging",
        TC_NOARCH: tc / "noarch",
        HOST_DIR_PORTABLE: host_dir_p,
        HOST_DIR_DISTRO: host_dir_d,
        HOST_DIR: host_dir,
      ) do
        yield tc
      end
    ensure
      saved_gcc_vers.each { |name, ver| ALL_ARCHS[name].gcc_ver = ver }
    end
  end

  # Stub the external I/O boundaries (Cache, run_command, system) so
  # tests can exercise real Package/PackageManager logic without
  # network access or real builds.
  #
  # Cache::download_file / download_git_repo → return true (skip download)
  # Cache::extract_file → create the target directory, return true
  # run_command → return true (or false if in fail_commands set)
  def with_stubbed_externals(fail_commands: Set.new)
    originals = {}

    # Save originals
    originals[:download_file] = Cache.method(:download_file)
    originals[:download_git_repo] = Cache.method(:download_git_repo)
    originals[:extract_file] = Cache.method(:extract_file)
    originals[:run_command] = method(:run_command)

    # Stub Cache::download_file — pretend the file exists in cache
    Cache.define_singleton_method(:download_file) { |url, remote, local = nil|
      local ||= remote
      FileUtils.touch(TC_CACHE / local)
      true
    }

    # Stub Cache::download_git_repo — same
    Cache.define_singleton_method(:download_git_repo) {
      |url, tarname, tag = nil, dir_name = nil|
      FileUtils.touch(TC_CACHE / tarname)
      true
    }

    # Stub Cache::extract_file — create the version directory
    Cache.define_singleton_method(:extract_file) { |tarfile, newDirName = nil|
      newDirName ||= "extracted"
      FileUtils.mkdir_p(newDirName)
      true
    }

    # Stub run_command (top-level method = private method on Object)
    Object.send(:define_method, :run_command) { |out, argv|
      cmd = argv.first.to_s
      !fail_commands.include?(cmd)
    }

    # Stub PackageManager#with_cc — yield the arch dir without
    # requiring a real compiler to be installed.
    pm = PackageManager.instance
    originals[:with_cc] = pm.method(:with_cc)
    pm.define_singleton_method(:with_cc) { |arch_name = nil, &block|
      arch = arch_name ? ALL_ARCHS[arch_name] : ARCH
      arch_dir = TC / "gcc-#{FAKE_GCC_VER}" / arch.name
      FileUtils.mkdir_p(arch_dir)
      block.call(arch_dir)
    }

    yield

  ensure
    # Restore originals
    Cache.define_singleton_method(:download_file, originals[:download_file])
    Cache.define_singleton_method(:download_git_repo,
                                  originals[:download_git_repo])
    Cache.define_singleton_method(:extract_file, originals[:extract_file])
    Object.send(:define_method, :run_command, originals[:run_command])
    pm = PackageManager.instance
    pm.define_singleton_method(:with_cc, originals[:with_cc]) if
      originals[:with_cc]
  end

  # A minimal Package subclass for testing. The only overrides are:
  #   - install_impl_internal: creates expected_files (no real build)
  #   - expected_files: configurable list
  #   - default_ver: stable fake version (not in pkg_versions)
  #
  # Everything else (install_impl, get_install_list, installed?,
  # default?, needs_upgrade?) runs the real base class code.
  class FakePackage < Package

    include FileShortcuts
    include FileUtilsShortcuts

    # Class-level install log — records the order of successful installs.
    @@install_log = []
    def self.install_log = @@install_log
    def self.clear_log! = @@install_log.clear

    def initialize(name, dep_list: [], arch_list: ALL_ARCHS,
                   on_host: false, is_compiler: false,
                   default: false, board_list: nil,
                   host_os_list: nil, host_arch_list: nil,
                   host_tier: :compiler)
      super(
        name: name,
        url: "https://fake/#{name}",
        on_host: on_host,
        is_compiler: is_compiler,
        host_tier: host_tier,
        arch_list: arch_list,
        dep_list: dep_list,
        host_os_list: host_os_list,
        host_arch_list: host_arch_list,
        default: default,
        board_list: board_list,
      )
    end

    def expected_files = []
    def tarname(ver) = "#{name}-#{ver}.tgz"
    def default_ver = Ver("1.0.0")

    # Match the pattern of real packages: host → syscc/HOST_ARCH,
    # noarch → nil/nil, target → gcc_ver/ARCH.
    def default_cc
      return "syscc" if on_host
      return nil if arch_list.nil?
      return ARCH.gcc_ver
    end

    def default_arch
      return HOST_ARCH if on_host
      return nil if arch_list.nil?
      return ARCH
    end

    def install_impl_internal(install_dir)
      @@install_log << name
      true
    end
  end
end
