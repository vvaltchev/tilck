# SPDX-License-Identifier: BSD-2-Clause
#
# A package can be complete, current, and still unusable.
#
# Nothing about it says so: the artifact is exactly what it should be,
# and `-l` used to draw it in green. What is gone is something it was
# built AGAINST -- the library it linked, the headers it compiled
# with -- and the first anyone hears of it is a later build failing on
# a path its flags still name.
#
# So the listing says it, and says what is missing, because the status
# cell has room for the word and the reason is the actionable half.
#

require_relative 'test_helper'
require 'stringio'

class TestUnusableInstalls < Minitest::Test

  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def capture_stdout(&block)
    old = $stdout
    $stdout = StringIO.new
    block.call
    $stdout.string
  ensure
    $stdout = old
  end

  # name => the packages it was built against, as words.
  def unusable_now
    installs, needs, missing = pkgmgr.install_graph
    return pkgmgr.unusable_installs(installs, needs, missing)
             .to_h { |i, why| [i.pkgname, why] }
  end

  # host_a depends on host_b depends on host_c, registered and then
  # installed bottom up, so that each one records what it was built
  # against the way a real install does.
  def chain(*names)

    prev = nil

    names.each { |n|
      deps = prev ? [Dep(prev, true)] : []
      pkgmgr.register(FakePackage.new(n, on_host: true, dep_list: deps))
      prev = n
    }

    for name, ver in pkgmgr.resolve_install_plan([[names.last, nil]]) do
      assert pkgmgr.install(name, ver), "#{name} failed to install"
    end

    pkgmgr.refresh
  end

  def test_an_install_whose_dependency_is_gone_cannot_be_used
    with_fake_tc do
      with_stubbed_externals do
        chain("host_lib", "host_app")

        assert_empty unusable_now, "both are here"

        pkgmgr.uninstall("host_lib", false, true)
        pkgmgr.refresh

        bad = unusable_now
        assert_equal ["host_app"], bad.keys
        assert_match(/\Ahost_lib /, bad["host_app"].first)
      end
    end
  end

  # It travels: what is built on something unusable is unusable too.
  # A gmp that is gone takes mpfr with it, and mpfr takes the gcc.
  def test_it_travels_up_the_chain
    with_fake_tc do
      with_stubbed_externals do
        chain("host_c", "host_b", "host_a")
        assert_empty unusable_now

        pkgmgr.uninstall("host_c", false, true)
        pkgmgr.refresh

        assert_equal %w[host_a host_b], unusable_now.keys.sort
      end
    end
  end

  # ...and it clears when the dependency comes back.
  def test_it_clears_when_the_dependency_returns
    with_fake_tc do
      with_stubbed_externals do
        chain("host_lib", "host_app")

        pkgmgr.uninstall("host_lib", false, true)
        pkgmgr.refresh
        refute_empty unusable_now

        pkgmgr.install("host_lib")
        pkgmgr.refresh
        assert_empty unusable_now
      end
    end
  end

  # A package with no dependencies cannot be waiting for anything.
  def test_a_package_with_no_dependencies_is_never_unusable
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("host_alone", on_host: true))
        pkgmgr.install("host_alone")
        pkgmgr.refresh
        assert_empty unusable_now
      end
    end
  end

  # The word in the cell, and the reason under the table.
  def test_the_listing_says_it_and_says_what_is_missing
    with_fake_tc do
      with_stubbed_externals do
        chain("host_lib", "host_app")
        pkgmgr.uninstall("host_lib", false, true)
        pkgmgr.refresh

        out = capture_stdout { pkgmgr.show_status_all }
                 .gsub(/\e\[[0-9;]*m/, "")

        assert_match(/host_app\s+\[\s*unusable/, out)
        assert_match(/Unusable: built correctly/, out)
        assert_match(/host_app .*needs host_lib /, out)
      end
    end
  end
end
