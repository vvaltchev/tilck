# SPDX-License-Identifier: BSD-2-Clause
#
# THE HALF OF AN INSTALL NO UNIT TEST REACHES.
#
# The ordinary harness stubs run_command, so every test that
# "installs" a package answers "did it build?" with a boolean and
# never runs a subprocess. Everything after that answer -- the
# staging directory, what a half-finished build leaves behind, the
# atomic move, resuming, composing a sysroot out of what is actually
# on disk -- is real code that only real installs execute.
#
# These packages build for real. Their build steps are mkdir and
# touch, so the whole file runs in about a second, but the machinery
# around them is the machinery that runs when GCC builds: same
# staging path, same move, same rollback, same failure handling.
#
# What this deliberately does NOT do is pretend to be a build. It
# cannot catch a configure option upstream removed, or a meson that
# refuses another meson's build directory, or an interpreter found on
# PATH -- five of this session's failures, every one of which needed
# the real QEMU. Fake packages exercise the ORCHESTRATION; only real
# builds exercise the contract with the outside world.
#

require_relative 'test_helper'

class TestOrchestration < Minitest::Test

  include TestHelper

  # A package that really builds: two commands, both real, producing
  # the file its expected_files names.
  class RealPackage < TestHelper::FakePackage

    # FakePackage's install_impl_internal logs the name and returns
    # true without building anything -- which is the whole point of it,
    # and the reason it cannot exercise any of this. These run the
    # steps for real, and still log, so ordering stays observable.
    def install_impl_internal(install_dir)
      TestHelper::FakePackage.install_log << name
      return run_build_steps(install_dir)
    end

    def build_steps = [
      Step("mkdir.log", ["mkdir", "-p", "$INSTALL/install/bin"]),
      Step("touch.log", ["touch", "$INSTALL/install/bin/#{name}"]),
    ]

    def expected_files(ver = nil) = [["install/bin/#{name}", false]]
  end

  # ...and one that fails partway, after making a mess.
  class FailingPackage < TestHelper::FakePackage

    def install_impl_internal(install_dir)
      TestHelper::FakePackage.install_log << name
      return run_build_steps(install_dir)
    end

    def build_steps = [
      Step("mkdir.log", ["mkdir", "-p", "$INSTALL/install/bin"]),
      Step("half.log", ["touch", "$INSTALL/install/bin/half-written"]),
      Step("boom.log", ["false"]),
    ]

    def expected_files(ver = nil) = [["install/bin/#{name}", false]]
  end

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # --- ordering ------------------------------------------------------

  # A diamond: top needs both sides, both sides need the base. The
  # base must be built once, and before anything that uses it.
  def test_a_diamond_is_built_bottom_up
    with_fake_tc do
      with_real_commands do
        pkgmgr.register(RealPackage.new("base"))
        pkgmgr.register(RealPackage.new("left", dep_list: [Dep("base", false)]))
        pkgmgr.register(RealPackage.new("right", dep_list: [Dep("base", false)]))
        pkgmgr.register(RealPackage.new("top",
                                        dep_list: [Dep("left", false),
                                                   Dep("right", false)]))

        plan = pkgmgr.resolve_install_plan([["top", nil]])
        for name, ver in plan do
          assert pkgmgr.install(name, ver), "#{name} failed to install"
        end

        log = FakePackage.install_log
        assert_equal 1, log.count("base"), "the base was built #{log.count("base")} times"
        assert log.index("base") < log.index("left"), "left before its base"
        assert log.index("base") < log.index("right"), "right before its base"
        assert log.index("left") < log.index("top"), "top before left"
        assert log.index("right") < log.index("top"), "top before right"
      end
    end
  end

  # --- the atomic move ------------------------------------------------

  # What the build produced is what the install contains, and the
  # staging tree is gone. Nothing is left half-moved.
  def test_a_successful_install_leaves_nothing_in_staging
    with_fake_tc do
      with_real_commands do
        pkg = RealPackage.new("movey")
        pkgmgr.register(pkg)

        assert pkgmgr.install("movey")
        pkgmgr.refresh

        inst = pkg.get_install_list.find { |i| !i.path.nil? }
        refute_nil inst, "nothing was installed"
        assert (inst.path / "install" / "bin" / "movey").file?,
               "the built file did not survive the move"

        staging = TC / "staging" / "movey"
        refute staging.directory?,
               "the staging tree outlived a successful install"
      end
    end
  end

  # --- failure --------------------------------------------------------

  # A build that dies partway has already written files. None of them
  # may appear as an installation: a half-built tree that looks
  # installed is worse than no tree at all, because the next thing to
  # use it fails somewhere unrelated.
  def test_a_failed_build_installs_nothing
    with_fake_tc do
      with_real_commands do
        pkg = FailingPackage.new("boomy")
        pkgmgr.register(pkg)

        refute pkgmgr.install("boomy"), "a failing build reported success"
        pkgmgr.refresh

        installed = pkg.get_install_list.reject { |i| i.path.nil? }
        assert_empty installed.map { |i| i.path.to_s },
                     "a failed build left an installation behind"
      end
    end
  end

  # ...and the mess it made is kept, not deleted: that is what makes
  # the next attempt a resume rather than a fresh download.
  def test_a_failed_build_keeps_its_staging_tree
    with_fake_tc do
      with_real_commands do
        pkgmgr.register(FailingPackage.new("boomy"))
        refute pkgmgr.install("boomy")

        half = TC / "staging" / "boomy" / "1.0.0" / "install" / "bin" /
               "half-written"
        assert half.file?,
               "the half-finished build was thrown away, so a retry " \
               "cannot resume"
      end
    end
  end

  # A second attempt picks the staging tree up rather than starting
  # over. The tell is in the log, because the outcome is the same
  # either way -- which is exactly why it needs a test.
  def test_a_second_attempt_resumes_from_staging
    with_fake_tc do
      with_real_commands do
        pkgmgr.register(FailingPackage.new("boomy"))
        refute pkgmgr.install("boomy")

        out = capture_stdout { pkgmgr.install("boomy") }
        assert_match(/Resuming from staging/, out,
                     "the second attempt started from scratch")
      end
    end
  end

  # --- the sysroot ----------------------------------------------------

  # The sysroot is a view over what is installed, composed from the
  # fragments each package publishes. Two packages, two fragments,
  # one farm -- and it has to be built from what is on disk rather
  # than from what the plan said would be.
  def test_the_sysroot_is_composed_from_what_is_installed
    with_fake_tc do
      with_real_commands do
        stack = Ver("13.3.0")

        a = RealPackage.new("host_a", on_host: true, host_tier: :stack,
                            arch_list: ALL_HOST_ARCHS.values)
        b = RealPackage.new("host_b", on_host: true, host_tier: :stack,
                            arch_list: ALL_HOST_ARCHS.values)

        for p in [a, b] do
          p.define_singleton_method(:sysroot_fragments) { |gcc_ver = nil|
            inst = find_install(default_ver)
            inst ? [[inst.path / "install" / "bin", "usr/bin"]] : []
          }
        end

        pkgmgr.register(a)
        pkgmgr.register(b)

        pkgmgr.with_host_stack(stack) do
          assert pkgmgr.install("host_a")
          assert pkgmgr.install("host_b")
          pkgmgr.refresh
          pkgmgr.compose_stack_sysroot(stack)
        end

        farm = pkgmgr.stack_coords(stack).sysroot / "usr" / "bin"
        assert (farm / "host_a").exist?, "host_a is missing from the sysroot"
        assert (farm / "host_b").exist?, "host_b is missing from the sysroot"
      end
    end
  end

  def capture_stdout(&block)
    old = $stdout
    $stdout = StringIO.new
    block.call
    $stdout.string
  ensure
    $stdout = old
  end
end
