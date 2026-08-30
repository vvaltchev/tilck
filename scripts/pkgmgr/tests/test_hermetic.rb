# SPDX-License-Identifier: BSD-2-Clause
#
# The hermetic host tier: packages that link against our own glibc and
# nothing from the system. See docs/plans/hermetic-host-toolchain.md.
#

require_relative 'test_helper'

class TestHermeticTier < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def hermetic_pkg(name = "host_h")
    return FakePackage.new(name, on_host: true, host_tier: :hermetic,
                           arch_list: ALL_HOST_ARCHS.values)
  end

  # The stack is keyed by OUR compiler's version, not the distro or the
  # system compiler: neither takes part in the build.
  def test_install_root_is_keyed_by_our_gcc_version
    with_fake_tc do
      pkg = hermetic_pkg
      gcc_ver = pkgmgr.get_config_ver("gcc", host: true).to_s

      root = pkg.host_install_root.to_s
      assert_match(%r{/hermetic/#{Regexp.escape(gcc_ver)}\z}, root)
      refute_match(/#{Regexp.escape(HOST_DISTRO)}/, root)
      refute_match(/#{Regexp.escape(HOST_CC)}/, root)
    end
  end

  def test_sysroot_sits_beside_the_packages
    with_fake_tc do
      pkg = hermetic_pkg
      assert_equal (pkg.hermetic_root / "sysroot").to_s,
                   pkg.hermetic_sysroot.to_s
    end
  end

  # Without HOST_VER_GCC every hermetic package would install to the
  # same truncated path, so this must fail loudly rather than build one.
  def test_missing_host_gcc_version_raises
    with_fake_tc do
      pkg = hermetic_pkg
      pm = PackageManager.instance
      orig = pm.method(:get_config_ver)
      begin
        pm.define_singleton_method(:get_config_ver) { |name, host:|
          name == "gcc" && host ? nil : orig.call(name, host: host)
        }
        e = assert_raises(RuntimeError) { pkg.hermetic_root }
        assert_match(/HOST_VER_GCC/, e.message)
      ensure
        pm.define_singleton_method(:get_config_ver, orig)
      end
    end
  end
end

class TestHermeticGating < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # A disabled package is invisible, not merely absent from the default
  # set: building gcc and glibc takes tens of minutes and must be asked
  # for, never stumbled into.
  def test_disabled_package_is_not_installable
    with_fake_tc do
      pkg = FakePackage.new("host_off", on_host: true,
                            arch_list: ALL_HOST_ARCHS.values)
      pkg.define_singleton_method(:enabled?) { false }
      pkgmgr.register(pkg)

      assert_empty pkg.get_installable_list
    end
  end

  def test_enabled_package_is_installable
    with_fake_tc do
      pkg = FakePackage.new("host_on", on_host: true,
                            arch_list: ALL_HOST_ARCHS.values)
      pkgmgr.register(pkg)

      refute_empty pkg.get_installable_list
    end
  end

  def test_install_refuses_a_disabled_package
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_off", on_host: true,
                              arch_list: ALL_HOST_ARCHS.values)
        pkg.define_singleton_method(:enabled?) { false }
        pkgmgr.register(pkg)

        refute pkgmgr.install("host_off")
        assert_empty FakePackage.install_log
      end
    end
  end

  def test_packages_are_enabled_by_default
    assert FakePackage.new("foo").enabled?
  end
end

class TestFinalInstallPrefix < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # A package that bakes an absolute --prefix into its output must be
  # given the path it will END UP at. Baking the staging path leaves it
  # pointing at a directory that stops existing the moment the atomic
  # move completes -- which is exactly what binutils did before this.
  def test_prefix_is_the_final_path_not_the_staging_one
    with_fake_tc do
      pkg = FakePackage.new("host_foo", on_host: true, host_tier: :distro)
      staging = pkg.staging_dir(Ver("1.0.0"))

      got = pkg.final_install_prefix(staging).to_s

      refute_match(/staging/, got)
      assert_match(%r{/foo/1\.0\.0/install\z}, got)
      assert got.start_with?(pkg.final_install_root.to_s)
    end
  end

  def test_prefix_follows_the_staged_version
    with_fake_tc do
      pkg = FakePackage.new("host_foo", on_host: true, host_tier: :distro)
      got = pkg.final_install_prefix(pkg.staging_dir(Ver("2.5.0"))).to_s
      assert_match(%r{/foo/2\.5\.0/install\z}, got)
    end
  end
end
