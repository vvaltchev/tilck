# SPDX-License-Identifier: BSD-2-Clause
#
# --clean: everything out, except what a clean must never take.
#
# The four flags that select "everything" (-u ALL -f -a ALL -c ALL)
# already worked, but -f is also what pulls the prebuilt compilers in,
# and nothing spared the Ruby the package manager is running on. So a
# full clean either kept too much or deleted its own interpreter.
#

require_relative 'test_helper'

class TestClean < Minitest::Test

  include TestHelper

  def target_dir(name, ver = "1.0.0")
    d = target_pkgs(ARCH, FAKE_GCC_VER.to_s) / name / ver
    FileUtils.mkdir_p(d)
    return d
  end

  def host_dir(name, ver = "1.0.0")
    d = distro_pkgs / name / ver
    FileUtils.mkdir_p(d)
    return d
  end

  def test_clean_removes_ordinary_packages
    with_fake_tc do
      reset_pkgmgr!
      pkgmgr.register(FakePackage.new("foo"))
      d = target_dir("foo")
      pkgmgr.refresh()

      n = pkgmgr.clean(false)

      assert_operator n, :>, 0
      refute d.directory?, "an ordinary package survived the clean"
    end
  end

  # The prebuilt cross-compilers are downloaded blobs. Nothing about
  # them goes stale in a way a rebuild fixes, and re-extracting them
  # is pure cost.
  def test_clean_spares_the_prebuilt_compilers
    with_fake_tc do
      with_stubbed_externals do
        reset_pkgmgr!
        cc = FakePackage.new("gcc-#{ARCH.name}-musl", on_host: true,
                             is_compiler: true, host_tier: :portable,
                             arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(cc)
        pkgmgr.register(FakePackage.new("foo"))

        d = portable_pkgs / "gcc-#{ARCH.name}-musl" / "1.0.0"
        FileUtils.mkdir_p(d)
        target_dir("foo")
        pkgmgr.refresh()

        pkgmgr.clean(false)
        assert d.directory?, "a prebuilt cross-compiler was removed"
      end
    end
  end

  # Removing the interpreter this is running on is at best pointless
  # churn: the bootstrap puts it straight back.
  def test_clean_spares_the_bootstrap_ruby
    with_fake_tc do
      reset_pkgmgr!
      d = host_dir("ruby", "3.4.7")     # an orphan: no package defines it
      target_dir("foo") if false
      pkgmgr.refresh()

      pkgmgr.clean(false)
      assert d.directory?, "the bootstrap Ruby was removed"
    end
  end

  # A dry run reports what it WOULD take, and takes nothing.
  def test_a_dry_clean_counts_without_removing
    with_fake_tc do
      reset_pkgmgr!
      pkgmgr.register(FakePackage.new("foo"))
      d = target_dir("foo")
      pkgmgr.refresh()

      n = pkgmgr.clean(true)

      assert_equal 1, n, "a dry run must report what it would remove"
      assert d.directory?, "a dry run removed something"
    end
  end

  # Nothing installed is not an error.
  def test_clean_on_an_empty_tree
    with_fake_tc do
      reset_pkgmgr!
      pkgmgr.refresh()
      assert_equal 0, pkgmgr.clean(false)
    end
  end

  #
  # Ruby survives ANY expression, not just --clean.
  #
  # It is the interpreter running this code, installed by the bash
  # bootstrap before any of this exists. There is no flag combination
  # that should let the package manager delete itself mid-job.
  #
  def test_no_expression_can_remove_ruby
    with_fake_tc do
      reset_pkgmgr!
      pkgmgr.register(FakePackage.new("foo"))
      ruby = host_dir("ruby", "3.4.7")   # an orphan: no package defines it
      other = target_dir("foo")
      pkgmgr.refresh()

      expressions = [
        # the most aggressive form there is
        ["ALL", true,  "ALL", "ALL", "ALL"],
        # ...and every weaker one
        ["ALL", false, "ALL", "ALL", "ALL"],
        ["ALL", true,  nil,   nil,   nil],
        # ...and asking for it by name, twice over
        ["ruby", true, "ALL", "ALL", "ALL"],
        ["ruby", true, Ver("3.4.7"), nil, nil],
      ]

      for name, force, ver, cc, arch in expressions do
        pkgmgr.uninstall(name, false, force, ver, cc, arch)
        assert ruby.directory?,
               "ruby was removed by: -u #{name} " \
               "#{force ? "-f " : ""}ver=#{ver.inspect} arch=#{arch.inspect}"
      end

      # ...while everything else really did go, so the expressions
      # above were not simply no-ops.
      refute other.directory?, "nothing was removed at all"
    end
  end

  # The cache is not spared by a rule -- it is spared by never being
  # anywhere the removal walks.
  def test_the_cache_is_not_inside_any_install_tree
    with_fake_tc do |tc|
      assert_equal TC_CACHE.to_s, (Pathname.new(tc) / "cache").to_s
      refute_includes TC_CACHE.to_s, "/pkgs/",
                      "the cache sits inside an install tree"
    end
  end
end
