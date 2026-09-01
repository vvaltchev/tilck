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
