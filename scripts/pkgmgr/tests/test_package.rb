# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'

# ---------------------------------------------------------------
# Tests for the pure-logic methods: these don't need filesystem
# or stubbed externals — they just check attribute-based decisions.
# ---------------------------------------------------------------

# The tier is one of four, and a package declaring another is refused
# when it is made. It used to get nil coordinates instead, and fail
# far away on the first path built from them.
class TestPackageHostTier < Minitest::Test

  include TestHelper

  def test_the_four_tiers_are_accepted
    for tier in Package::HOST_TIERS do
      pkg = FakePackage.new("host_t", on_host: true, host_tier: tier)
      assert_equal tier, pkg.host_tier
    end
  end

  def test_an_unknown_tier_is_refused_at_construction
    e = assert_raises(ArgumentError) {
      FakePackage.new("host_t", on_host: true, host_tier: :portabel)
    }
    assert_match(/host_t: host_tier :portabel is not one of/, e.message)
    assert_match(/:portable, :distro, :compiler, :stack/, e.message)
  end

  # ...and the table of coordinates says so too, for a caller that is
  # not a registered package.
  def test_the_coordinate_table_refuses_what_is_not_a_tier
    assert_raises(ArgumentError) {
      Package.host_coords(:portabel, Host.env)
    }
    assert_raises(ArgumentError) { Package.host_coords(:stack, Host.env) }
  end
end

class TestPackageHostSupported < Minitest::Test
  include TestHelper

  def test_no_constraints
    pkg = FakePackage.new("foo")
    assert bound(pkg).host_supported?
  end

  def test_matching_os
    pkg = FakePackage.new("host_foo", on_host: true,
                          host_os_list: [HOST_OS])
    assert bound(pkg).host_supported?
  end

  def test_wrong_os
    pkg = FakePackage.new("host_foo", on_host: true,
                          host_os_list: ["nope_os"])
    refute bound(pkg).host_supported?
  end

  def test_matching_arch
    pkg = FakePackage.new("host_foo", on_host: true,
                          host_arch_list: [HOST_ARCH.name])
    assert bound(pkg).host_supported?
  end

  def test_wrong_arch
    pkg = FakePackage.new("host_foo", on_host: true,
                          host_arch_list: ["nope_arch"])
    refute bound(pkg).host_supported?
  end

  def test_both_constraints_match
    pkg = FakePackage.new("host_foo", on_host: true,
                          host_os_list: [HOST_OS],
                          host_arch_list: [HOST_ARCH.name])
    assert bound(pkg).host_supported?
  end
end

class TestPackageBoardSupported < Minitest::Test
  include TestHelper

  def test_nil_means_any
    pkg = FakePackage.new("foo")
    assert pkg.board_supported?
  end

  def test_matching_board
    with_context(BOARD: "test-board") do
      pkg = FakePackage.new("foo", board_list: ["test-board"])
      assert bound(pkg).board_supported?
    end
  end

  def test_wrong_board
    with_context(BOARD: "other-board") do
      pkg = FakePackage.new("foo", board_list: ["test-board"])
      refute bound(pkg).board_supported?
    end
  end

  def test_nil_board_with_board_list
    with_context(BOARD: nil) do
      pkg = FakePackage.new("foo", board_list: ["test-board"])
      refute bound(pkg).board_supported?
    end
  end
end

class TestPackageArchSupported < Minitest::Test
  include TestHelper

  def test_all_archs
    pkg = FakePackage.new("foo", arch_list: ALL_ARCHS.values)
    assert bound(pkg).arch_supported?
  end

  def test_nil_means_noarch
    pkg = FakePackage.new("foo", arch_list: nil)
    assert pkg.arch_supported?
  end

  def test_host_package_always_true
    pkg = FakePackage.new("host_foo", on_host: true,
                          arch_list: ALL_HOST_ARCHS.values)
    assert pkg.arch_supported?
  end

  def test_wrong_arch
    with_context(ARCH: ALL_ARCHS["i386"]) do
      pkg = FakePackage.new("foo",
                            arch_list: Archs("riscv64"))
      refute bound(pkg).arch_supported?
    end
  end

  def test_matching_arch
    with_context(ARCH: ALL_ARCHS["i386"]) do
      pkg = FakePackage.new("foo",
                            arch_list: Archs("i386"))
      assert bound(pkg).arch_supported?
    end
  end
end

class TestPackageDefault < Minitest::Test
  include TestHelper

  def test_false_by_default
    refute FakePackage.new("foo").default?
  end

  def test_true_when_set
    assert bound(FakePackage.new("foo", default: true)).default?
  end

  def test_gated_by_arch
    with_context(ARCH: ALL_ARCHS["i386"]) do
      pkg = FakePackage.new("foo", default: true,
                            arch_list: Archs("riscv64"))
      refute bound(pkg).default?
    end
  end

  def test_gated_by_host_os
    pkg = FakePackage.new("host_foo", on_host: true, default: true,
                          host_os_list: ["nope_os"])
    refute bound(pkg).default?
  end

  def test_gated_by_board
    with_context(BOARD: "other-board") do
      pkg = FakePackage.new("foo", default: true,
                            board_list: ["test-board"])
      refute bound(pkg).default?
    end
  end

  def test_passes_all_gates
    pkg = FakePackage.new("foo", default: true,
                          arch_list: [ARCH])
    assert bound(pkg).default?
  end
end

# ---------------------------------------------------------------
# Tests that exercise real Package code paths with a fake TC
# directory and stubbed externals.
# ---------------------------------------------------------------

class TestPackageInstallReal < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_install_creates_dir_and_records
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        result = pkgmgr.install("foo")
        assert result
        assert_equal ["foo"], FakePackage.install_log
        assert bound(pkg).installed?(Ver("1.0.0"))
      end
    end
  end

  def test_install_already_installed_skips
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)

        # First install
        pkgmgr.install("foo")
        FakePackage.clear_log!

        # Second install — should skip
        result = pkgmgr.install("foo")
        assert result  # nil (skip) is truthy via the nil? ternary
        assert_empty FakePackage.install_log
      end
    end
  end

  def test_install_host_not_supported_fails_early
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true,
                              host_os_list: ["nope_os"])
        pkgmgr.register(pkg)
        result = bound(pkg).install_impl(Ver("1.0.0"))
        assert_equal false, result
        assert_empty FakePackage.install_log
      end
    end
  end

  def test_install_board_not_supported_fails_early
    with_fake_tc do |tc|
      with_context(BOARD: "other") do
        with_stubbed_externals do
          pkg = FakePackage.new("foo", board_list: ["test-board"])
          pkgmgr.register(pkg)
          result = bound(pkg).install_impl(Ver("1.0.0"))
          assert_equal false, result
          assert_empty FakePackage.install_log
        end
      end
    end
  end

  def test_install_unknown_package_returns_false
    reset_pkgmgr!
    result = pkgmgr.install("nonexistent")
    assert_equal false, result
  end

  def test_install_arch_mismatch_returns_false
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo",
                              arch_list: Archs("riscv64"))
        pkgmgr.register(pkg)
        with_context(ARCH: ALL_ARCHS["i386"]) do
          result = pkgmgr.install("foo")
          assert_equal false, result
        end
      end
    end
  end
end

class TestPackageNeedsUpgrade < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
  end

  def test_not_installed
    with_fake_tc do
      pkg = FakePackage.new("foo")
      refute bound(pkg).needs_upgrade?
    end
  end

  def test_current_version_installed
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")  # installs at default_ver (1.0.0)
        refute bound(pkg).needs_upgrade?
      end
    end
  end

  def test_old_version_installed
    with_fake_tc do |tc|
      # Simulate a previous default install at an older version.
      # It carries no origin file, like every install made before
      # that file existed, and so reads as a default install.
      gcc_ver = ARCH.gcc_ver.to_s
      old_dir = target_pkgs(ARCH, gcc_ver) / "foo" / "0.9.0"
      FileUtils.mkdir_p(old_dir)

      pkg = FakePackage.new("foo")
      pkgmgr.register(pkg)
      assert bound(pkg).needs_upgrade?
    end
  end

  #
  # The distinction the marker exists for. On disk a default install
  # and an explicitly-requested one are both just <pkg>/<ver>/; only
  # the marker says which is which, and --upgrade must not replace a
  # version somebody asked for by name.
  #
  def test_default_install_is_marked
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")               # no version named

        inst = bound(pkg).find_install(Ver("1.0.0"))
        assert_equal "default manual",
                     (inst.path / InstallOrigin::FILE).read.strip
        assert inst.default_install
      end
    end
  end

  def test_explicit_version_install_is_not_marked
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo", Ver("1.0.0"))  # version named explicitly

        inst = bound(pkg).find_install(Ver("1.0.0"))
        assert_equal "pinned manual",
                     (inst.path / InstallOrigin::FILE).read.strip
        refute inst.default_install
      end
    end
  end

  def test_pinned_old_version_is_left_alone
    with_fake_tc do |tc|
      # Same old version on disk as test_old_version_installed, but
      # recorded as pinned: somebody asked for 0.9.0 by name, so a
      # later default bump must not drag them off it.
      gcc_ver = ARCH.gcc_ver.to_s
      old_dir = target_pkgs(ARCH, gcc_ver) / "foo" / "0.9.0"
      FileUtils.mkdir_p(old_dir)
      InstallOrigin.write(old_dir, false, true)

      pkg = FakePackage.new("foo")
      pkgmgr.register(pkg)
      refute bound(pkg).needs_upgrade?
    end
  end

  # An installation predating the origin file reads as a default one,
  # so --upgrade keeps working on a toolchain built before it existed.
  def test_install_without_origin_file_reads_as_default
    with_fake_tc do |tc|
      gcc_ver = ARCH.gcc_ver.to_s
      old_dir = target_pkgs(ARCH, gcc_ver) / "foo" / "0.9.0"
      FileUtils.mkdir_p(old_dir)
      refute (old_dir / InstallOrigin::FILE).exist?

      pkg = FakePackage.new("foo")
      pkgmgr.register(pkg)
      assert bound(pkg).find_install(Ver("0.9.0")).default_install
      assert bound(pkg).needs_upgrade?
    end
  end

  def test_current_default_install_needs_no_upgrade
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")
        refute bound(pkg).needs_upgrade?
      end
    end
  end

  # A pinned old version sitting next to a marked current one must not
  # make the package look upgradable.
  def test_pinned_old_beside_marked_current
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo", Ver("0.9.0"))  # pinned
        pkgmgr.install("foo")                # default (1.0.0), marked

        refute bound(pkg).needs_upgrade?
      end
    end
  end
end

class TestPackageConfigure < Minitest::Test
  include TestHelper

  def test_not_configurable_by_default
    refute FakePackage.new("foo").configurable?
  end
end
