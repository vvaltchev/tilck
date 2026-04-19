# SPDX-License-Identifier: BSD-2-Clause
#
# Exhaustive tests for the install flow across all package types.
#

require_relative 'test_helper'

class TestInstallTargetPackage < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_target_pkg_creates_correct_dir
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")

        gcc = FAKE_GCC_VER.to_s
        ver_dir = tc / "gcc-#{gcc}" / ARCH.name / "foo" / "1.0.0"
        assert ver_dir.directory?
      end
    end
  end

  def test_install_records_in_get_install_list
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")

        list = pkg.get_install_list
        assert_equal 1, list.length
        assert_equal Ver("1.0.0"), list.first.ver
        assert_equal ARCH, list.first.arch
        refute list.first.broken
      end
    end
  end

  def test_install_idempotent
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        FakePackage.clear_log!

        pkgmgr.install("foo")
        assert_empty FakePackage.install_log
      end
    end
  end
end

class TestInstallHostPackage < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_host_pkg_creates_correct_dir
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true,
                              arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(pkg)
        pkgmgr.install("host_foo")

        # Non-portable host packages go under HOST_DIR/pkg_dirname/ver
        host_dir = HOST_DIR
        ver_dir = host_dir / "foo" / "1.0.0"
        assert ver_dir.directory?
      end
    end
  end

  def test_portable_host_pkg_uses_portable_dir
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true, host_tier: :portable,
                              arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(pkg)
        pkgmgr.install("host_foo")

        ver_dir = HOST_DIR_PORTABLE / "foo" / "1.0.0"
        assert ver_dir.directory?
      end
    end
  end

  def test_host_pkg_install_list_detects_installed
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true,
                              arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(pkg)
        pkgmgr.install("host_foo")

        list = pkg.get_install_list
        assert_equal 1, list.length
        assert_equal "syscc", list.first.compiler
        assert_equal HOST_ARCH, list.first.arch
      end
    end
  end
end

class TestInstallNoarchPackage < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_noarch_pkg_creates_correct_dir
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("noarch_foo", arch_list: nil)
        pkgmgr.register(pkg)
        pkgmgr.install("noarch_foo")

        ver_dir = tc / "noarch" / "noarch_foo" / "1.0.0"
        assert ver_dir.directory?
      end
    end
  end

  def test_noarch_install_list
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("noarch_foo", arch_list: nil)
        pkgmgr.register(pkg)
        pkgmgr.install("noarch_foo")

        list = pkg.get_install_list
        assert_equal 1, list.length
        assert_nil list.first.compiler
        assert_nil list.first.arch
      end
    end
  end
end

class TestInstallDependencyOrder < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_linear_chain_install_order
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("a",
          dep_list: [Dep("b", false)]))
        pkgmgr.register(FakePackage.new("b",
          dep_list: [Dep("c", false)]))
        pkgmgr.register(FakePackage.new("c"))

        plan = pkgmgr.resolve_install_plan([["a", nil]])
        assert_equal ["c", "b", "a"], plan.map(&:first)
      end
    end
  end

  def test_diamond_deps_install_order
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("a",
          dep_list: [Dep("b", false), Dep("c", false)]))
        pkgmgr.register(FakePackage.new("b",
          dep_list: [Dep("d", false)]))
        pkgmgr.register(FakePackage.new("c",
          dep_list: [Dep("d", false)]))
        pkgmgr.register(FakePackage.new("d"))

        plan = pkgmgr.resolve_install_plan([["a", nil]])
        names = plan.map(&:first)
        assert_equal "d", names.first
        assert_equal "a", names.last
      end
    end
  end

  def test_installed_dep_skipped
    with_fake_tc do
      with_stubbed_externals do
        pkg_b = FakePackage.new("b")
        pkgmgr.register(FakePackage.new("a",
          dep_list: [Dep("b", false)]))
        pkgmgr.register(pkg_b)

        # Install b first
        pkgmgr.install("b")

        plan = pkgmgr.resolve_install_plan([["a", nil]])
        names = plan.map(&:first)
        assert_equal ["a"], names  # b already installed, only a needed
      end
    end
  end

  def test_implicit_compiler_dep_in_plan
    with_fake_tc do
      with_stubbed_externals do
        cc_name = "gcc-#{ARCH.name}-musl"
        pkgmgr.register(FakePackage.new(cc_name, on_host: true,
                                        is_compiler: true,
                                        arch_list: ALL_HOST_ARCHS.values))
        pkgmgr.register(FakePackage.new("foo"))

        plan = pkgmgr.resolve_install_plan([
          [cc_name, nil], ["foo", nil]
        ])
        names = plan.map(&:first)
        # Compiler must come before target package
        assert_operator names.index(cc_name), :<, names.index("foo")
      end
    end
  end

  def test_multiple_packages_install_order
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("a"))
        pkgmgr.register(FakePackage.new("b"))
        pkgmgr.register(FakePackage.new("c"))

        plan = pkgmgr.resolve_install_plan([
          ["c", nil], ["a", nil], ["b", nil]
        ])
        # No deps → alphabetical order
        assert_equal ["a", "b", "c"], plan.map(&:first)
      end
    end
  end
end

class TestInstallConstraints < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_install_wrong_arch_fails
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("rv_only",
          arch_list: Archs("riscv64")))
        with_context(ARCH: ALL_ARCHS["i386"]) do
          result = pkgmgr.install("rv_only")
          assert_equal false, result
          assert_empty FakePackage.install_log
        end
      end
    end
  end

  def test_install_wrong_host_os_fails
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("host_foo", on_host: true,
                                        host_os_list: ["nope_os"]))
        result = pkgmgr.install("host_foo")
        assert_equal false, result
      end
    end
  end

  def test_install_wrong_board_fails
    with_context(BOARD: "other") do
      with_fake_tc do
        with_stubbed_externals do
          pkgmgr.register(FakePackage.new("board_pkg",
                                          board_list: ["myboard"]))
          result = pkgmgr.install("board_pkg")
          assert_equal false, result
        end
      end
    end
  end
end

# Guard around the atomic staging→final mv in Package#install_impl.
#
# Without the guard, Ruby's FileUtils.mv falls back to POSIX semantics
# ("mv src existing_dir/" moves src INSIDE dst) when final_ver_dir
# already exists, silently producing `final/<ver>/<ver>` corruption.
# This happens any time installed? returns false while the install
# dir is physically there — for example, after the user runs
# `make distclean` inside an install tree, or after a partially-
# failed uninstall.
class TestInstallMvGuard < Minitest::Test
  include TestHelper

  # FakePackage variant that writes a real marker file in its install
  # dir, so `check_install_dir` can be made to pass or fail by
  # creating / deleting that file.
  class FakePackageWithMarker < TestHelper::FakePackage
    def expected_files = [["marker.txt", false]]

    def install_impl_internal(install_dir)
      super(install_dir)
      File.write(install_dir / "marker.txt", "installed\n")
      true
    end
  end

  # Lets a test force installed?(ver) to return false even when the
  # package's install dir is structurally complete on disk. Needed to
  # exercise the defensive "final exists and is not broken" branch,
  # which is hard to reach by any natural sequence of install actions
  # (installed? short-circuits first in the normal flow).
  class ForcedMissingFakePackage < FakePackageWithMarker
    attr_accessor :pretend_not_installed

    def installed?(ver)
      return false if @pretend_not_installed
      super
    end
  end

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_broken_final_dir_is_removed_before_mv
    # Regression: a corrupted install (expected file deleted from
    # final_ver_dir) was re-triggering install_impl, which then mv'd
    # staging INSIDE the existing final dir — producing
    # final/<ver>/<ver>/marker.txt nesting.
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackageWithMarker.new("foo")
        pkgmgr.register(pkg)
        assert pkgmgr.install("foo")

        gcc = FAKE_GCC_VER.to_s
        final = tc / "gcc-#{gcc}" / ARCH.name / "foo" / "1.0.0"
        assert (final / "marker.txt").file?
        refute pkg.get_install_list.any?(&:broken)

        # Simulate corruption (e.g. make distclean in the install dir).
        FileUtils.rm(final / "marker.txt")
        assert pkg.get_install_list.any?(&:broken),
               "install should now look broken"

        # Re-install: guard should remove the broken final dir and
        # then mv staging in cleanly.
        FakePackage.clear_log!
        assert pkgmgr.install("foo")

        assert_includes FakePackage.install_log, "foo"
        assert (final / "marker.txt").file?,
               "marker should be restored by the fresh install"
        refute (final / "1.0.0").directory?,
               "nested <ver>/<ver> must NOT exist"
      end
    end
  end

  def test_non_broken_final_dir_refuses_without_force
    # Defensive branch: final_ver_dir exists with all expected files
    # but installed? returned false. Without -f (which pre-uninstalls
    # at the CLI layer), install_impl should refuse rather than
    # silently clobber the valid install.
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = ForcedMissingFakePackage.new("foo")
        pkgmgr.register(pkg)
        assert pkgmgr.install("foo")

        gcc = FAKE_GCC_VER.to_s
        final = tc / "gcc-#{gcc}" / ARCH.name / "foo" / "1.0.0"
        marker_mtime_before = File.mtime(final / "marker.txt")

        # Force the bad state: pkgmgr sees the pkg as NOT installed
        # even though the files on disk are intact.
        pkg.pretend_not_installed = true
        FakePackage.clear_log!

        result = pkgmgr.install("foo")
        assert_equal false, result,
                     "install should refuse, not silently overwrite"
        # The original install is untouched (marker still has its
        # original content and mtime — the mv never happened). The
        # staging build did run (install_impl_internal writes to
        # staging before the guard kicks in at the mv step), but
        # nothing reached final_ver_dir.
        assert (final / "marker.txt").file?
        assert_equal marker_mtime_before, File.mtime(final / "marker.txt")
        refute (final / "1.0.0").directory?,
               "nested <ver>/<ver> must NOT exist"
      end
    end
  end
end
