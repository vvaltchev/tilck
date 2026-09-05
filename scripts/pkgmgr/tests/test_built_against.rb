# SPDX-License-Identifier: BSD-2-Clause
#
# WHAT AN INSTALL WAS BUILT AGAINST.
#
# Three answers, in order: the version the request resolved, the
# dependency's own pin, the dependency's default. Recorded at install
# time (built_against), read back for a rebuild (deps_of_install) --
# where the record wins, then the pin, then the one version present,
# then the default. Seven mutants of those two ladders survived the
# suite; each rung is a test here.
#

require_relative 'test_helper'

class TestBuiltAgainst < Minitest::Test

  include TestHelper

  HOST = { on_host: true, host_tier: :distro,
           arch_list: ALL_HOST_ARCHS.values }.freeze

  def setup
    reset_pkgmgr!
    @gmp = FakePackage.new("host_gmp", **HOST)
    @gmp.define_singleton_method(:default_ver) { Ver("2.0.0") }
    @gmp.define_singleton_method(:installable_versions) {
      [Ver("1.0.0"), Ver("2.0.0")]
    }
    @user = FakePackage.new("host_user", dep_list: [Dep("host_gmp", true)],
                            **HOST)
    @pinner = FakePackage.new("host_pinner", **HOST,
                              dep_list: [Dep("host_gmp", true,
                                             ver: Ver("1.0.0"))])
    [@gmp, @user, @pinner].each { |p| pkgmgr.register(p) }
  end

  def v(s) = Ver(s)

  # --- the resolution context ---------------------------------------------

  def test_the_previous_resolution_comes_back_after_the_block
    pkgmgr.with_resolved_versions({ "a" => v("1.0.0") }) do
      pkgmgr.with_resolved_versions({ "a" => v("9.9.9") }) { }
      assert_equal v("1.0.0"), pkgmgr.resolved_ver("a"),
                   "the inner context replaced the outer with nothing"
    end
    assert_nil pkgmgr.resolved_ver("a")
  end

  # --- built_against: what gets recorded ----------------------------------

  def test_the_resolved_version_is_recorded_first
    pkgmgr.with_resolved_versions({ "host_gmp" => v("1.0.0") }) do
      assert_equal({ "host_gmp" => v("1.0.0") },
                   pkgmgr.built_against(@user, @user.default_ver))
    end
  end

  def test_without_a_resolution_the_dependencys_pin_is_recorded
    assert_equal({ "host_gmp" => v("1.0.0") },
                 pkgmgr.built_against(@pinner, @pinner.default_ver))
  end

  def test_without_either_the_dependencys_default_is_recorded
    assert_equal({ "host_gmp" => v("2.0.0") },
                 pkgmgr.built_against(@user, @user.default_ver))
  end

  # --- deps_of_install: what a rebuild builds against ---------------------

  def install_of(pkg)
    fake_install(pkg)
    return pkg.find_install(pkg.default_ver)
  end

  def test_the_record_wins_over_what_is_installed
    with_fake_tc do
      inst = install_of(@user)
      InstallDeps.write(inst.path, { "host_gmp" => v("1.0.0") })
      fake_install(@gmp, v("2.0.0"))   # the only gmp present is not it

      versions, ambiguous = pkgmgr.deps_of_install(@user, inst)
      assert_equal({ "host_gmp" => v("1.0.0") }, versions)
      assert_empty ambiguous
    end
  end

  def test_without_a_record_the_pin_wins_over_what_is_installed
    with_fake_tc do
      inst = install_of(@pinner)          # a fake install has no record
      fake_install(@gmp, v("2.0.0"))

      versions, = pkgmgr.deps_of_install(@pinner, inst)
      assert_equal({ "host_gmp" => v("1.0.0") }, versions)
    end
  end

  def test_without_a_record_a_pin_or_an_install_the_default_is_taken
    with_fake_tc do
      inst = install_of(@user)            # a fake install has no record

      versions, ambiguous = pkgmgr.deps_of_install(@user, inst)
      assert_equal({ "host_gmp" => v("2.0.0") }, versions)
      assert_empty ambiguous
    end
  end
end

# replace with nothing to replace is an install. Found as a surviving
# mutant: the early return read as nothing, and no test noticed.
class TestReplaceWithNothingThere < Minitest::Test

  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # A tree another run left under staging/replaced -- interrupted
  # between setting it aside and putting it back -- is left alone,
  # and does not make this replace fail on its way out.
  def test_a_stranded_tree_under_replaced_is_left_alone
    with_fake_tc do
      with_stubbed_externals do
        stranded = TC_STAGING / "replaced" / "other" / "1.0.0"
        FileUtils.mkdir_p(stranded)
        File.write(stranded / "keep", "")

        pkg = FakePackage.new("fresh")
        pkgmgr.register(pkg)
        run_cli("-s", "fresh")
        assert pkgmgr.replace(pkg, pkg.default_ver, default_install: true)

        assert (stranded / "keep").file?, "the stranded tree was taken"
        refute (TC_STAGING / "replaced" / "fresh").exist?
      end
    end
  end

  def test_it_installs
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("fresh")
        pkgmgr.register(pkg)
        assert_nil pkg.find_install(pkg.default_ver)

        assert pkgmgr.replace(pkg, pkg.default_ver, default_install: true)
        assert_equal ["fresh"], FakePackage.install_log
        refute_nil pkg.find_install(pkg.default_ver)
      end
    end
  end
end
