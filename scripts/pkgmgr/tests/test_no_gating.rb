# SPDX-License-Identifier: BSD-2-Clause
#
# Every package in the tree can be asked for by name.
#
# The portable host stack used to be hidden behind TILCK_HOST_STACK:
# thirty-odd packages -- glibc, GCC, the whole GTK closure, QEMU --
# answered
#
#   Package host_qemu is not enabled in this configuration
#   The host toolchain is opt-in: set TILCK_HOST_STACK=1
#
# to a plain `-s host_qemu`, and were invisible to `-l`. The intent
# was to keep a multi-hour libc build from happening by accident, but
# `default: false` already does exactly that: nothing pulls those
# packages in on its own. The switch did not guard the accident, it
# blocked the deliberate request -- the one case where the user has
# named the package.
#
# So the invariant is now simply that there is no such switch. If a
# gate comes back, this fails and names the package it hides.
#

require_relative 'test_helper'
require_relative '../main'

class TestNothingIsGatedOff < Minitest::Test

  # Snapshotted at LOAD time: reset_pkgmgr! empties the registry for
  # whichever test wants a clean one, and with a randomised order,
  # reading it from inside a test reads whatever ran before.
  PACKAGES = pkgmgr.all_packages.dup.freeze

  def test_every_package_can_be_asked_for_by_name
    gated = PACKAGES.reject(&:enabled?).map(&:name)
    assert_empty gated, "packages that -s would refuse"
  end

  # The stack is reachable, and staying out of the default set is what
  # keeps it from building itself. Both halves matter: enabled without
  # default? means "you may ask"; default? would mean "you get it
  # whether you asked or not".
  def test_the_expensive_ones_are_reachable_but_not_default
    names = [
      "host_binutils", "host_glibc", "host_gcc",
      "host_gtk3", "host_qemu",
    ]

    for n in names do
      p = PACKAGES.find { |x| x.name == n }
      refute_nil p, "#{n} is not registered"
      assert p.enabled?, "#{n} must be installable without a switch"
      refute p.default?, "#{n} must not build itself unasked"
    end
  end
end
