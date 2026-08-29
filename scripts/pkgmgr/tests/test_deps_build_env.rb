# SPDX-License-Identifier: BSD-2-Clause
#
# Package-level tests for the build-interface abstraction:
#
#   Package#find_install / #install_prefix  — which install do we mean
#   Package#build_env                       — what a provider publishes
#   Package#deps_build_env                  — what a consumer collects
#
# The point of the abstraction is that no package name appears in the
# base class and no consumer names a dependency: adding a library to
# dep_list is all it takes for its flags to show up.
#

require_relative 'test_helper'

# A package that publishes a build interface, the way host_ncurses does.
class ProviderPackage < TestHelper::FakePackage

  def build_env(ver)
    prefix = install_prefix(ver)
    return BuildEnv.new(
      include_dirs:    [prefix / "include"],
      lib_dirs:        [prefix / "lib"],
      pkg_config_dirs: [prefix / "lib" / "pkgconfig"],
    )
  end
end

# A provider whose flags differ by version — the case that motivated
# making build_env take `ver` rather than reading a global.
class VersionedProviderPackage < TestHelper::FakePackage

  def build_env(ver)
    prefix = install_prefix(ver)
    dirs = [prefix / "include"]
    dirs << prefix / "include" / "wide" if ver >= Ver("2.0.0")
    return BuildEnv.new(include_dirs: dirs)
  end
end

class TestFindInstall < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_find_install_returns_matching_version
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true, host_tier: :distro)
        pkgmgr.register(pkg)
        pkg.install_impl(Ver("1.0.0"))

        info = pkg.find_install(Ver("1.0.0"))
        refute_nil info
        assert_equal Ver("1.0.0"), info.ver
      end
    end
  end

  def test_find_install_nil_when_not_installed
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true, host_tier: :distro)
        pkgmgr.register(pkg)
        assert_nil pkg.find_install(Ver("1.0.0"))
      end
    end
  end

  def test_find_install_nil_for_other_version
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true, host_tier: :distro)
        pkgmgr.register(pkg)
        pkg.install_impl(Ver("1.0.0"))
        assert_nil pkg.find_install(Ver("9.9.9"))
      end
    end
  end

  #
  # The regression that motivated replacing
  #
  #   info = pkg.get_install_list.find { |x| !x.broken }
  #
  # with a version-keyed lookup. get_install_list is built by walking
  # <root>/<pkg>/<ver>/ with Dir.children, which returns filesystem
  # order — so "the first one that isn't broken" is whichever version
  # the directory happens to list first, and that changes as unrelated
  # packages are installed and removed.
  #
  def test_find_install_picks_the_asked_version_with_several_installed
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true, host_tier: :distro)
        pkgmgr.register(pkg)
        pkg.install_impl(Ver("1.0.0"))
        pkg.install_impl(Ver("2.0.0"))
        pkg.install_impl(Ver("3.0.0"))

        assert_equal 3, pkg.get_install_list.length

        for v in ["1.0.0", "2.0.0", "3.0.0"]
          assert_equal Ver(v), pkg.find_install(Ver(v)).ver
        end
      end
    end
  end

  def test_installed_agrees_with_find_install
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true, host_tier: :distro)
        pkgmgr.register(pkg)
        pkg.install_impl(Ver("1.0.0"))

        assert pkg.installed?(Ver("1.0.0"))
        assert !pkg.installed?(Ver("2.0.0"))
      end
    end
  end
end

class TestInstallPrefix < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_install_prefix_points_at_the_version_dir
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true, host_tier: :distro)
        pkgmgr.register(pkg)
        pkg.install_impl(Ver("1.0.0"))

        prefix = pkg.install_prefix(Ver("1.0.0"))
        assert prefix.directory?
        assert_equal "1.0.0", prefix.basename.to_s
      end
    end
  end

  def test_install_prefix_distinguishes_versions
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true, host_tier: :distro)
        pkgmgr.register(pkg)
        pkg.install_impl(Ver("1.0.0"))
        pkg.install_impl(Ver("2.0.0"))

        p1 = pkg.install_prefix(Ver("1.0.0"))
        p2 = pkg.install_prefix(Ver("2.0.0"))
        refute_equal p1.to_s, p2.to_s
      end
    end
  end

  # Dependency resolution guarantees the dep is installed, so a miss is
  # a bug to report — never a cue to fall back on whatever the host
  # system happens to provide.
  def test_install_prefix_raises_with_actionable_message
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true, host_tier: :distro)
        pkgmgr.register(pkg)

        e = assert_raises(RuntimeError) { pkg.install_prefix(Ver("1.0.0")) }
        assert_match(/foo/, e.message)
        assert_match(/1\.0\.0/, e.message)
        assert_match(/not installed/, e.message)
        assert_match(/build_toolchain -s host_foo/, e.message)
      end
    end
  end
end

class TestPackageBuildEnv < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_base_class_publishes_nothing
    with_fake_tc do
      pkg = FakePackage.new("host_foo", on_host: true, host_tier: :distro)
      pkgmgr.register(pkg)
      assert pkg.build_env(Ver("1.0.0")).empty?
    end
  end

  def test_base_class_build_env_does_not_need_an_install
    with_fake_tc do
      pkg = FakePackage.new("host_foo", on_host: true, host_tier: :distro)
      pkgmgr.register(pkg)
      # No install on disk, and no raise: a package that publishes
      # nothing never looks its install tree up.
      assert_equal BuildEnv.empty.include_dirs,
                   pkg.build_env(Ver("1.0.0")).include_dirs
    end
  end

  def test_provider_publishes_its_own_paths
    with_fake_tc do
      with_stubbed_externals do
        pkg = ProviderPackage.new("host_prov", on_host: true, host_tier: :distro)
        pkgmgr.register(pkg)
        pkg.install_impl(Ver("1.0.0"))

        be = pkg.build_env(Ver("1.0.0"))
        prefix = pkg.install_prefix(Ver("1.0.0"))
        assert_equal ["#{prefix}/include"], be.include_dirs
        assert_equal ["#{prefix}/lib"], be.lib_dirs
        assert_equal ["#{prefix}/lib/pkgconfig"], be.pkg_config_dirs
      end
    end
  end

  def test_provider_can_vary_flags_by_version
    with_fake_tc do
      with_stubbed_externals do
        pkg = VersionedProviderPackage.new("host_vp", on_host: true,
                                           host_tier: :distro)
        pkgmgr.register(pkg)
        pkg.install_impl(Ver("1.0.0"))
        pkg.install_impl(Ver("2.0.0"))

        assert_equal 1, pkg.build_env(Ver("1.0.0")).include_dirs.length
        assert_equal 2, pkg.build_env(Ver("2.0.0")).include_dirs.length
      end
    end
  end
end

class TestDepsBuildEnv < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # Host packages, so nothing drags a cross-compiler into the graph.
  # Package#initialize asserts that a host package's name starts with
  # "host_", so the prefix is added here to keep the tests readable.
  def host_pkg(klass, name, deps: [])
    return klass.new("host_#{name}", on_host: true, host_tier: :distro,
                     dep_list: deps.map { |d| Dep("host_#{d}", true) })
  end

  def test_no_deps_gives_empty_env
    with_fake_tc do
      c = host_pkg(TestHelper::FakePackage, "consumer")
      pkgmgr.register(c)
      assert c.deps_build_env.empty?
    end
  end

  def test_single_provider_dep
    with_fake_tc do
      with_stubbed_externals do
        p = host_pkg(ProviderPackage, "prov")
        c = host_pkg(TestHelper::FakePackage, "consumer", deps: ["prov"])
        pkgmgr.register(p)
        pkgmgr.register(c)
        p.install_impl(Ver("1.0.0"))

        be = c.deps_build_env
        prefix = p.install_prefix(Ver("1.0.0"))
        assert_equal ["#{prefix}/include"], be.include_dirs
        assert_equal ["HOSTCFLAGS=-I#{prefix}/include",
                      "HOSTLDFLAGS=-L#{prefix}/lib"], be.kconfig_make_vars
      end
    end
  end

  def test_dep_that_publishes_nothing_contributes_nothing
    with_fake_tc do
      with_stubbed_externals do
        q = host_pkg(TestHelper::FakePackage, "quiet")
        c = host_pkg(TestHelper::FakePackage, "consumer", deps: ["quiet"])
        pkgmgr.register(q)
        pkgmgr.register(c)
        q.install_impl(Ver("1.0.0"))

        assert c.deps_build_env.empty?
      end
    end
  end

  def test_two_providers_merge_into_one_assignment_each
    with_fake_tc do
      with_stubbed_externals do
        a = host_pkg(ProviderPackage, "pa")
        b = host_pkg(ProviderPackage, "pb")
        c = host_pkg(TestHelper::FakePackage, "consumer", deps: ["pa", "pb"])
        [a, b, c].each { |p| pkgmgr.register(p) }
        a.install_impl(Ver("1.0.0"))
        b.install_impl(Ver("1.0.0"))

        vars = c.deps_build_env.kconfig_make_vars
        assert_equal 1, vars.count { |v| v.start_with?("HOSTCFLAGS=") }
        assert_equal 1, vars.count { |v| v.start_with?("HOSTLDFLAGS=") }

        cflags = vars.find { |v| v.start_with?("HOSTCFLAGS=") }
        assert_match(/pa/, cflags)
        assert_match(/pb/, cflags)
      end
    end
  end

  def test_transitive_dep_is_included
    with_fake_tc do
      with_stubbed_externals do
        deep = host_pkg(ProviderPackage, "deep")
        mid  = host_pkg(TestHelper::FakePackage, "mid", deps: ["deep"])
        c    = host_pkg(TestHelper::FakePackage, "consumer", deps: ["mid"])
        [deep, mid, c].each { |p| pkgmgr.register(p) }
        deep.install_impl(Ver("1.0.0"))
        mid.install_impl(Ver("1.0.0"))

        be = c.deps_build_env
        assert_equal 1, be.include_dirs.length
        assert_match(/deep/, be.include_dirs.first)
      end
    end
  end

  def test_direct_dep_comes_before_transitive_one
    with_fake_tc do
      with_stubbed_externals do
        near = host_pkg(ProviderPackage, "near")
        far  = host_pkg(ProviderPackage, "far")
        mid  = host_pkg(TestHelper::FakePackage, "mid", deps: ["far"])
        c = host_pkg(TestHelper::FakePackage, "consumer",
                     deps: ["near", "mid"])
        [near, far, mid, c].each { |p| pkgmgr.register(p) }
        [near, far, mid].each { |p| p.install_impl(Ver("1.0.0")) }

        dirs = c.deps_build_env.include_dirs
        assert_equal 2, dirs.length
        assert_match(/near/, dirs[0])
        assert_match(/far/, dirs[1])
      end
    end
  end

  def test_shared_dep_reached_twice_contributes_once
    with_fake_tc do
      with_stubbed_externals do
        shared = host_pkg(ProviderPackage, "shared")
        l = host_pkg(TestHelper::FakePackage, "left", deps: ["shared"])
        r = host_pkg(TestHelper::FakePackage, "right", deps: ["shared"])
        c = host_pkg(TestHelper::FakePackage, "consumer",
                     deps: ["left", "right"])
        [shared, l, r, c].each { |p| pkgmgr.register(p) }
        [shared, l, r].each { |p| p.install_impl(Ver("1.0.0")) }

        dirs = c.deps_build_env.include_dirs
        assert_equal 1, dirs.length
        assert_match(/shared/, dirs.first)
      end
    end
  end

  #
  # Integration-level version of the Dir.children ordering bug: with
  # several versions of a provider on disk, the consumer must get the
  # one bound for it (today: the provider's default_ver), not whichever
  # the filesystem lists first.
  #
  def test_consumer_gets_the_bound_version_not_an_arbitrary_one
    with_fake_tc do
      with_stubbed_externals do
        p = host_pkg(ProviderPackage, "prov")
        c = host_pkg(TestHelper::FakePackage, "consumer", deps: ["prov"])
        pkgmgr.register(p)
        pkgmgr.register(c)

        # default_ver for FakePackage is 1.0.0; install newer ones too.
        p.install_impl(Ver("1.0.0"))
        p.install_impl(Ver("2.0.0"))
        p.install_impl(Ver("3.0.0"))
        assert_equal 3, p.get_install_list.length

        dirs = c.deps_build_env.include_dirs
        assert_equal 1, dirs.length
        assert_match(%r{/1\.0\.0/include\z}, dirs.first)
        refute_match(/2\.0\.0/, dirs.first)
        refute_match(/3\.0\.0/, dirs.first)
      end
    end
  end

  def test_deps_build_env_is_deterministic_across_calls
    with_fake_tc do
      with_stubbed_externals do
        a = host_pkg(ProviderPackage, "pa")
        b = host_pkg(ProviderPackage, "pb")
        c = host_pkg(TestHelper::FakePackage, "consumer", deps: ["pa", "pb"])
        [a, b, c].each { |p| pkgmgr.register(p) }
        a.install_impl(Ver("1.0.0"))
        b.install_impl(Ver("1.0.0"))

        first = c.deps_build_env.include_dirs
        5.times { assert_equal first, c.deps_build_env.include_dirs }
      end
    end
  end

  def test_uninstalled_provider_dep_raises_instead_of_degrading
    with_fake_tc do
      with_stubbed_externals do
        p = host_pkg(ProviderPackage, "prov")
        c = host_pkg(TestHelper::FakePackage, "consumer", deps: ["prov"])
        pkgmgr.register(p)
        pkgmgr.register(c)
        # prov is never installed.

        e = assert_raises(RuntimeError) { c.deps_build_env }
        assert_match(/prov/, e.message)
        assert_match(/not installed/, e.message)
      end
    end
  end
end
