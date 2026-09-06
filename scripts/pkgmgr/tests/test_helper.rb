# SPDX-License-Identifier: BSD-2-Clause

require 'minitest/autorun'

# Object#stub and Minitest::Mock come from minitest/mock, which older
# minitest releases happened to load from autorun and newer ones do not.
# Require it explicitly: relying on the transitive load made the suite
# pass on the local Ruby and fail on every CI distro image.
require 'minitest/mock'
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

#
# A TEST MAY NOT READ THE DEVELOPER'S TOOLCHAIN.
#
# Two tests asked the real tree whether host_python was installed.
# They passed here, where it is, and errored on all six CI images at
# once, where it is not -- and neither one was about installation at
# all: one checked a published bin dir, the other a PATH order. A test
# that consults toolchain5/ is not testing the code, it is reporting
# what this machine last built, and it does so silently until the day
# it runs somewhere else.
#
# So the reading is made impossible rather than discouraged.
# get_install_list and get_installable_list are where every such
# question funnels -- find_install, installed?, install_prefix,
# build_env, python_interpreter, refresh, the whole listing -- so the
# guard sits there and names the caller. with_fake_tc builds a world
# to ask about; with_real_tc is the deliberate opt-out, for a test
# whose subject IS the real tree.
#
REAL_TC = TC

module NoRealToolchainReads

  @@allowed = false

  def self.allowed? = @@allowed
  def self.allow!(v) = @@allowed = v

  def get_install_list
    NoRealToolchainReads.check!("#{name}.get_install_list")
    return super
  end

  def get_installable_list
    NoRealToolchainReads.check!("#{name}.get_installable_list")
    return super
  end

  def self.check!(what)

    return if @@allowed || TC != REAL_TC

    raise "#{what} would read the real toolchain at #{TC}. A test must " \
          "build the world it asks about: wrap it in with_fake_tc, or in " \
          "with_real_tc when the real tree is the subject."
  end
end

Package.prepend(NoRealToolchainReads)

require_relative 'laws'
require_relative 'model/bridge'
require 'stringio'

module TestHelper

  # Run one command line through Main.main and return [rc, stdout].
  #
  # A command line can exit rather than return, and a test process
  # must survive that: without the rescue, one `exit 1` deep inside an
  # argument check takes the whole suite down with no output at all.
  #
  # The laws (tests/laws.rb) are checked around every run made inside
  # a fake toolchain -- there is no world to read outside one. A test
  # that must switch them off says why: `laws: false, because: "..."`.
  def run_cli(*argv, laws: :auto, because: nil)

    argv = argv.flatten.map(&:to_s)
    checking = laws == true || (laws == :auto && TC != REAL_TC)

    if laws == false && because.nil?
      raise ArgumentError, "run_cli(laws: false) needs a because:"
    end

    require_relative '../main'
    before = checking ? Bridge.snapshot : nil

    old = $stdout
    $stdout = StringIO.new

    # A copy: parse_options consumes argv in place, and the laws need
    # the line as it was typed.
    begin
      rc = Main.main(argv.dup)
    rescue SystemExit => e
      rc = e.status
    ensure
      out = $stdout.string
      $stdout = old
    end

    if checking
      after = Bridge.snapshot
      broken = Laws.check(argv, before, after)
      assert broken.empty?,
             "the command line broke a law:\n" +
             broken.map(&:to_s).join("\n\n")
    end

    return [rc, out]
  end

  # For a test whose subject really is the installed tree.
  def with_real_tc
    prev = NoRealToolchainReads.allowed?
    NoRealToolchainReads.allow!(true)

    begin
      yield
    ensure
      NoRealToolchainReads.allow!(prev)
    end
  end

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

    # -H sets this and never restores it -- correct for a one-shot
    # command line, and a landmine for a test process, where one
    # `run_cli("-H", "7.7.7", ...)` silently moved the stack for
    # every test that ran afterwards.
    pm.instance_variable_set(:@portable_stack, nil)
    pm.instance_variable_set(:@host_world, nil)
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

  # The pkgs/ directory of a tier, named the way tests used to name
  # the old HOST_DIR* constants. Everything derives from TC now, so
  # these are conveniences rather than configuration.
  def portable_pkgs = Coords.new(HOST_OS_ARCH, nil, nil).pkgs_dir
  def distro_pkgs   = Coords.new(HOST_OS_ARCH, HOST_DISTRO, nil).pkgs_dir
  def hostcc_pkgs   = Coords.new(HOST_OS_ARCH, HOST_DISTRO, HOST_CC).pkgs_dir
  def stack_pkgs(v) = Coords.new(HOST_OS_ARCH, nil, "gcc-#{v}").pkgs_dir

  # Target and noarch package dirs. Tests used to spell these out as
  # tc/"gcc-<ver>"/<arch>/..., which is why a layout change broke
  # forty of them at once.
  def target_pkgs(arch = ARCH, gcc = nil, board = :default)
    gcc ||= arch.gcc_ver
    board = arch.default_board if board == :default
    return Coords.new("tilck-#{arch.name}", board, "gcc-#{gcc}").pkgs_dir
  end

  def noarch_pkgs = Coords.new("noarch", nil, nil).pkgs_dir

  # An installation of `pkg` in the world the test is building.
  #
  # Everything expected_files names is created, because that list is
  # what decides `broken`, and a broken install is invisible to
  # find_install -- so a directory alone answers "not installed" and
  # the test looks like a bug in the code it is exercising.
  #
  #   at:      explicit coordinates (another board, another stack);
  #            default: where the package puts `ver` in this scope
  #   record:  :ok (a record matching the recipe), :changed (one that
  #            does not), :missing (none)
  #   origin:  :default or :pinned, what .install_origin says
  #
  def fake_install(pkg, ver = nil, at: nil, record: :ok, origin: :default)

    ver ||= pkg.default_ver
    dir = at ? pkg.pkg_dir_at(at) / pkg.ver_dirname(ver) : pkg.install_dir(ver)
    FileUtils.mkdir_p(dir)

    for name, is_dir in pkg.expected_files(ver) do
      path = dir / name

      if is_dir
        FileUtils.mkdir_p(path)
      else
        FileUtils.mkdir_p(path.dirname)
        FileUtils.touch(path)
        FileUtils.chmod(0755, path)
      end
    end

    InstallOrigin.write(dir, origin == :default)
    pkgmgr.refresh

    case record
    when :ok
      inst = pkg.get_install_list.find { |i| i.path == dir }
      raise "fake_install: #{dir} is not seen by #{pkg.name}" if inst.nil?
      pkg.write_build_inputs(inst)
    when :changed
      File.write(dir / BuildInputs::FILE,
                 "recipe sha256:not-what-it-was-built-from\n")
    when :missing
      nil
    else
      raise ArgumentError, "record: #{record.inspect}"
    end

    pkgmgr.refresh
    return dir
  end

  # A patch directory for `pkg`, in a temporary tree.
  #
  # Tests used to mkdir under the REAL scripts/patches/ and remove the
  # version directory afterwards, which left the package directory
  # behind: an empty scripts/patches/foo/ sat in the source tree, and
  # would have been applied to the next package that took the name.
  # A test has no business writing there at all.
  def with_fake_patches(pkg, ver = "1.0.0")
    Dir.mktmpdir("pkgmgr-patches-") do |root|
      r = Pathname.new(root)
      pkg.define_singleton_method(:patch_root) { r }
      dir = pkg.patch_base(Ver(ver))
      FileUtils.mkdir_p(dir)
      yield dir
    end
  end

  def with_fake_tc
    Dir.mktmpdir("pkgmgr-test-") do |dir|
      tc = Pathname.new(dir)
      FileUtils.mkdir_p(tc / "cache")
      FileUtils.mkdir_p(tc / "staging")

      # Set gcc_ver for all architectures (normally done by main.rb's
      # read_gcc_ver_defaults, which tests don't call).
      saved_gcc_vers = ALL_ARCHS.map { |name, arch| [name, arch.gcc_ver] }
      ALL_ARCHS.each_value { |arch| arch.gcc_ver = FAKE_GCC_VER }

      # Only TC and its two non-install directories need overriding:
      # every install location is derived from TC through Coords, so
      # redirecting the root redirects all of them. Under toolchain4
      # this block had to name five separate path constants, and the
      # day one of them was missed the tests deleted the developer's
      # real sysroot.
      with_context(
        ARCH: FAKE_ARCH,
        BOARD: FAKE_ARCH.default_board,
        DEFAULT_BOARD: FAKE_ARCH.default_board,
        TC: tc,
        TC_CACHE: tc / "cache",
        TC_STAGING: tc / "staging",
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
  # Like with_stubbed_externals, but run_command is NOT stubbed.
  #
  # Downloads and extraction stay faked -- no network, no tarballs --
  # while everything after them is the real thing: real subprocesses,
  # real exit codes, the real staging directory, the real atomic move.
  # That is the half of an install no unit test reaches, because the
  # ordinary harness answers "did it build?" with a boolean before any
  # of it runs.
  def with_real_commands(&block)
    originals = {
      download_file: Cache.method(:download_file),
      download_git_repo: Cache.method(:download_git_repo),
      extract_file: Cache.method(:extract_file),
    }

    Cache.define_singleton_method(:download_file) { |url, remote, local = nil|
      FileUtils.touch(TC_CACHE / (local || remote))
      true
    }

    Cache.define_singleton_method(:download_git_repo) {
      |url, tarname, tag = nil, dir_name = nil|
      FileUtils.touch(TC_CACHE / tarname)
      true
    }

    Cache.define_singleton_method(:extract_file) { |tarfile, newDirName = nil|
      FileUtils.mkdir_p(newDirName || "extracted")
      true
    }

    pm = PackageManager.instance
    originals[:with_cc] = pm.method(:with_cc)
    pm.define_singleton_method(:with_cc) { |arch_name = nil, &blk|
      arch = arch_name ? ALL_ARCHS[arch_name] : ARCH
      dir = Coords.new("tilck-#{arch.name}", arch.default_board,
                       "gcc-#{FAKE_GCC_VER}").pkgs_dir
      FileUtils.mkdir_p(dir)
      blk.call(dir)
    }

    block.call
  ensure
    Cache.define_singleton_method(:download_file, originals[:download_file])
    Cache.define_singleton_method(:download_git_repo,
                                  originals[:download_git_repo])
    Cache.define_singleton_method(:extract_file, originals[:extract_file])
    PackageManager.instance.define_singleton_method(:with_cc,
                                                    originals[:with_cc])
  end

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
    # The same signature as the real one, env: included. A stub that
    # takes fewer arguments than what it replaces passes every test
    # and breaks the moment production uses the argument it does not
    # know about -- which is what a stub is supposed to prevent.
    Object.send(:define_method, :run_command) { |out, argv, env: nil|
      cmd = argv.first.to_s
      !fail_commands.include?(cmd)
    }

    # Stub PackageManager#with_cc — yield the arch dir without
    # requiring a real compiler to be installed.
    pm = PackageManager.instance
    originals[:with_cc] = pm.method(:with_cc)
    pm.define_singleton_method(:with_cc) { |arch_name = nil, &block|
      arch = arch_name ? ALL_ARCHS[arch_name] : ARCH
      arch_dir = Coords.new("tilck-#{arch.name}", arch.default_board,
                            "gcc-#{FAKE_GCC_VER}").pkgs_dir
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

    def initialize(name, dep_list: [], arch_list: ALL_ARCHS.values,
                   on_host: false, is_compiler: false,
                   default: false, board_list: nil,
                   host_os_list: nil, host_arch_list: nil,
                   host_tier: :compiler, source: :default,
                   target_arch: nil, libc: nil)
      # target_arch: makes this fake a CROSS COMPILER, the way
      # GccPackage is one -- its installs carry the target metadata,
      # which is what the listing reads to tell a toolchain from an
      # ordinary package. Without it a fake with is_compiler: true
      # still produces plain installs, so it cannot stand in for one.
      @fake_target_arch = target_arch
      @fake_libc = libc
      # source: :default -> auto-build a fake SourceRef from the name.
      # source: nil      -> explicit no source (for testing vendor/blob-
      #                     style packages with a custom install_impl).
      # source: <ref>    -> caller-provided SourceRef (e.g. to test
      #                     shared sources across packages).
      if source == :default
        source = SourceRef.new(name: name, url: "https://fake/#{name}")
      end
      super(
        name: name,
        source: source,
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

    def expected_files(ver = nil) = []
    def default_ver = Ver("1.0.0")

    # The same wrap GccPackage applies, for the same reason: an
    # install of a cross compiler has to say what it targets.
    def get_install_list
      return super if @fake_target_arch.nil?

      super.map { |i|
        InstallInfo.new(
          i.pkgname, i.compiler, i.on_host, i.arch, i.ver, i.path,
          i.pkg, i.broken, @fake_target_arch, @fake_libc,
          default_install: i.default_install, coords: i.coords
        )
      }
    end

    # Match the pattern of real packages: noarch → nil, target →
    # pkgmgr.target_arch (respects with_target_arch scope, defaults to
    # ARCH). Host packages defer to the base, where the tier decides
    # between the system compiler and the stack's own.
    def default_cc
      return super if on_host
      return nil if arch_list.nil?
      return pkgmgr.target_arch.gcc_ver
    end

    def default_arch
      return HOST_ARCH if on_host
      return nil if arch_list.nil?
      return pkgmgr.target_arch
    end

    def install_impl_internal(install_dir)
      @@install_log << name
      true
    end
  end
end
