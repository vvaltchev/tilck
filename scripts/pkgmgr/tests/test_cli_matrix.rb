# SPDX-License-Identifier: BSD-2-Clause
#
# THE COMMAND LINE, NOT THE LIBRARY UNDER IT.
#
# test_uninstall.rb is one of the larger files here and it never
# caught any of the three -u bugs this tree has had, because it calls
# pkgmgr.uninstall(...) directly -- below the layer where all three
# lived. The whole suite drives Main.main 26 times, and not once with
# -u:
#
#   -s ...        9 invocations
#   -l ...        3
#   --upgrade     3
#   --check-for-updates  2
#   -C ...        2
#   -u ...        0
#
# The bugs were in the argument-computing layer: force_remove passing
# nil where it meant "any compiler", uninstall defaulting the compiler
# through a "syscc" test that stopped being true, -f running outside
# the stack scope. Every one of them is invisible from below and
# obvious from above.
#
# So these tests run the CLI and then look at the TREE. Not at what
# was printed -- at which installations still exist. A command that
# claims success while removing the wrong directory fails here.
#

require_relative 'test_helper'
require_relative '../main'
require 'stringio'

class TestCliMatrix < Minitest::Test

  include TestHelper

  RV = "riscv64"

  def setup
    reset_pkgmgr!
  end

  # A command line can exit rather than return, and a test process
  # must survive that: without the rescue, one `exit 1` deep inside an
  # argument check takes the whole suite down with no output at all,
  # which is how this harness first behaved.
  def run_cli(*argv)
    old = $stdout
    $stdout = StringIO.new

    begin
      rc = Main.main(argv)
    rescue SystemExit => e
      rc = e.status
    end

    [rc, $stdout.string]
  ensure
    $stdout = old
  end

  # Every installation on disk, by path. The assertions below compare
  # these sets, because "what survived" is the only question that
  # matters after a removal.
  def snapshot
    pkgmgr.refresh
    pkgmgr.all_packages.flat_map { |p|
      p.get_install_list.reject { |i| i.path.nil? }.map { |i| i.path.to_s }
    }.sort
  end

  # A target package installed for three arches, one of which has two
  # boards, plus a second version. Four coordinates and two versions,
  # which is enough for every filter to be wrong in a visible way.
  def install_target_spread(name = "spread")
    pkg = FakePackage.new(name)
    pkgmgr.register(pkg)

    with_context(ARCH: ALL_ARCHS["i386"], BOARD: nil) {
      pkgmgr.install(name)
    }
    with_context(ARCH: ALL_ARCHS["x86_64"], BOARD: nil) {
      pkgmgr.install(name)
    }
    with_context(ARCH: ALL_ARCHS[RV], BOARD: "qemu-virt") {
      pkgmgr.install(name)
    }
    with_context(ARCH: ALL_ARCHS[RV], BOARD: "licheerv-nano") {
      pkgmgr.install(name)
    }

    pkgmgr.refresh
    return pkg
  end

  def paths_for(pkg, &filter)
    pkgmgr.refresh
    pkg.get_install_list.reject { |i| i.path.nil? }
       .select { |i| filter.nil? || filter.call(i) }
       .map { |i| i.path.to_s }
  end

  # --- -u, the mode with no CLI test at all -------------------------

  # The default is the coordinates the invocation names, and nothing
  # else. `-u zlib` on riscv64 took the licheerv-nano copy with it for
  # as long as this tree has had two boards.
  def test_u_removes_only_the_current_coordinates
    with_fake_tc do
      with_stubbed_externals do
        pkg = install_target_spread
        before = snapshot

        rc, out = with_context(ARCH: ALL_ARCHS[RV], BOARD: "qemu-virt") {
          run_cli("-u", "spread", "-q")
        }
        assert_equal 0, rc, out

        gone = before - snapshot
        assert_equal 1, gone.length,
                     "removed #{gone.length} trees, not one:\n#{gone.join("\n")}"
        assert_includes gone.first, "qemu-virt"
      end
    end
  end

  def test_u_with_all_arches_removes_every_coordinate
    with_fake_tc do
      with_stubbed_externals do
        install_target_spread
        rc, _ = run_cli("-u", "spread", "-a", "ALL", "-q")

        assert_equal 0, rc
        assert_empty snapshot.select { |p| p.include?("/spread/") },
                     "-a ALL left something behind"
      end
    end
  end

  # A dry run is a question, not a command.
  def test_u_dry_run_removes_nothing
    with_fake_tc do
      with_stubbed_externals do
        install_target_spread
        before = snapshot

        rc, out = run_cli("-u", "spread", "-a", "ALL", "-d", "-q")

        assert_equal 0, rc
        assert_equal before, snapshot, "-d removed something"
        assert_match(/DRY RUN/, out)
      end
    end
  end

  # Naming a version that is not installed must remove nothing. It
  # once removed every version of the package instead.
  def test_u_with_an_absent_version_removes_nothing
    with_fake_tc do
      with_stubbed_externals do
        install_target_spread
        before = snapshot

        rc, out = run_cli("-u", "spread:9.9.9", "-q")

        assert_equal 0, rc
        assert_equal before, snapshot,
                     "a version that is not installed took the ones that are"
        assert_match(/not installed|nothing matched/, out)
      end
    end
  end

  # A host :stack package lives under the compiler that built it, so
  # -H picks which installation the command means.
  def test_u_of_a_stack_package_follows_the_stack
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_thing", on_host: true,
                              host_tier: :stack,
                              arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(pkg)

        a = Ver("7.7.7")
        b = Ver("8.8.8")

        # -H names a stack, and a stack is named by a compiler: the
        # option checks the version against what the compiler package
        # says it can build.
        gcc = FakePackage.new("host_gcc", on_host: true,
                              host_tier: :distro,
                              arch_list: ALL_HOST_ARCHS.values)
        gcc.define_singleton_method(:installable_versions) { [a, b] }
        pkgmgr.register(gcc)
        pkgmgr.with_host_stack(a) { pkgmgr.install("host_thing") }
        pkgmgr.with_host_stack(b) { pkgmgr.install("host_thing") }
        pkgmgr.refresh

        before = snapshot
        rc, _ = run_cli("-H", a.to_s, "-u", "host_thing", "-q")
        assert_equal 0, rc

        gone = before - snapshot
        assert_equal 1, gone.length, "removed #{gone.length} trees, not one"
        assert_includes gone.first, "gcc-7.7.7"
      end
    end
  end

  # --- -s ------------------------------------------------------------

  # -f is a removal followed by an install, and both halves have to
  # mean the same installation. When they did not, the run announced
  # a removal and then said "already installed" -- twice, for two
  # different reasons.
  def test_s_force_actually_reinstalls
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")
        pkgmgr.refresh

        FakePackage.clear_log!
        rc, out = run_cli("-s", "foo", "-f", "-q")

        assert_equal 0, rc
        assert_includes FakePackage.install_log, "foo",
                        "-f reported success without building anything"
        refute_match(/already installed/, out)
      end
    end
  end

  # --- -l --------------------------------------------------------------

  def test_l_shows_every_arch_an_install_exists_for
    with_fake_tc do
      with_stubbed_externals do
        install_target_spread
        _, out = run_cli("-l", "-q")
        plain = out.gsub(/\e\[[0-9;]*m/, "")

        line = plain.lines.find { |l| l.start_with?("spread") }
        refute_nil line, "the package is not listed at all"

        for arch in ["i386", "x86_64", RV] do
          assert_includes line, arch, "#{arch} is missing from the line"
        end
      end
    end
  end

  def test_l_group_by_ver_keeps_the_versions_distinct
    with_fake_tc do
      with_stubbed_externals do
        install_target_spread
        _, out = run_cli("-l", "-g", "ver", "-q")
        plain = out.gsub(/\e\[[0-9;]*m/, "")

        line = plain.lines.find { |l| l.start_with?("spread") }
        assert_includes line, "1.0.0"
      end
    end
  end
end
