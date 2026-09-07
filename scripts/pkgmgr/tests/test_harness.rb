# SPDX-License-Identifier: BSD-2-Clause
#
# Tests for the test harness itself.
#
# The suite runs on whatever Ruby the machine provides: the one pkgmgr
# builds locally, and the distro's own on each CI image. Anything the
# tests rely on that a Ruby release is free to move has to be asserted
# here, so that a missing piece is reported once, by name, instead of
# as a pile of NoMethodErrors in whichever file happened to use it.
#

require_relative 'test_helper'

class TestHarnessCapabilities < Minitest::Test

  # minitest/mock. Some releases load it from minitest/autorun and
  # some do not; test_helper requires it explicitly, and this is what
  # says so out loud.
  def test_object_stub_is_available
    assert_respond_to Object.new, :stub
    assert_respond_to Process, :stub, "module-level stubbing is used too"
  end

  def test_minitest_mock_is_available
    assert defined?(Minitest::Mock), "Minitest::Mock is not loaded"
  end

  # The stub has to actually take effect, not merely exist: a stub
  # that silently no-ops would turn every test using it into a test of
  # the real system.
  def test_a_stub_replaces_the_method_for_the_block
    outside = Process.respond_to?(:uid) ? Process.uid : nil
    Process.stub(:uid, 4242) do
      assert_equal 4242, Process.uid
    end
    assert_equal outside, Process.uid
  end
end

#
# The system tests run in this same process, after the unit lane, and
# read the real tree from end to end. What they inherit is a world
# built for unit tests: a registry holding whatever fakes the last
# test registered, and the guard that forbids reading toolchain5/.
# TestHelper#real_world! hands the real one back, and SystemTests.run
# asks for it before anything else.
#
# It went unnoticed for seven commits, because the lane costs hours
# and nobody had run it since the guard was added. Both ways it fails
# are here: the registry with something in it raises, and the registry
# empty is worse -- host_world_names computes [] from it, and the wipe
# that means to keep the host world takes it instead.
#
class TestTheRealWorldIsHandedBack < Minitest::Test

  include TestHelper

  def test_a_unit_test_may_not_read_the_real_toolchain
    refute NoRealToolchainReads.allowed?
  end

  def test_real_world_replaces_the_fakes_and_lifts_the_guard
    saved = pkgmgr.all_packages.dup
    was_allowed = NoRealToolchainReads.allowed?

    # The unit lane, as it leaves the process behind.
    reset_pkgmgr!
    pkgmgr.register(FakePackage.new("gcc-i386-musl", on_host: true,
                                    is_compiler: true,
                                    arch_list: ALL_HOST_ARCHS.values))

    real_world!

    pkgs = pkgmgr.all_packages
    assert_equal REAL_PACKAGES.length, pkgs.length
    assert_empty pkgs.select { |p| p.is_a?(FakePackage) },
                 "a fake from the unit lane survived into the real world"

    # What wipe_toolchain keeps. Computed from the registry alone, so
    # an empty one does not fail: it answers "keep nothing".
    refute_empty pkgmgr.host_world_names,
                 "the wipe would remove the host world with this keep-list"

    assert NoRealToolchainReads.allowed?,
           "the lane cannot read the tree it is installing into"

  ensure
    NoRealToolchainReads.allow!(was_allowed)
    reset_pkgmgr!
    saved.each { |p| pkgmgr.register(p) }
  end
end
