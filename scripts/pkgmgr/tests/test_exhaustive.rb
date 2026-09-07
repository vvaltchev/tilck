# SPDX-License-Identifier: BSD-2-Clause
#
# THE EXHAUSTIVE LANE, SAMPLED.
#
# The full enumeration (tests/exhaustive/) is some three hundred
# thousand cases and belongs to CI: `-t --exhaustive`. The default
# suite runs a fixed-seed sample of it here, so that every local run
# still asks the model a thousand questions the tests did not think
# of, in about the time the rest of the suite takes.
#
#   --seed N     a different sample (the seed is printed on failure)
#   --case ID    one case, by the id a failure prints
#
# The runner's self-test runs first. A comparison that cannot find a
# subject equal to itself reports nothing.
#

require_relative 'test_helper'
require_relative 'exhaustive/runner'

class TestExhaustive < Minitest::Test

  SAMPLE = 1000

  def seed = ($exhaustive_seed || 20260903).to_i

  def test_the_instrument_passes_its_self_test
    assert_empty Exhaustive.self_test
  end

  def test_a_sample_of_every_shape_agrees_with_the_model
    ids = $exhaustive_case ? [$exhaustive_case]
                           : Exhaustive.sample_ids(SAMPLE, seed: seed)
    failed = []

    for id in ids do
      r = Exhaustive.run_case(Exhaustive.case_by_id(id))
      failed << r if !r.ok
    end

    assert_empty failed,
                 "#{failed.length} of #{ids.length} cases disagree with " \
                 "the model (seed #{seed}; replay one with " \
                 "--case ID):\n\n" +
                 failed.first(5).map(&:to_s).join("\n\n")
  end

  # The tables are the same whatever stack the process is in when they
  # are first asked for: the stack compiler's default version follows
  # the stack in effect, and the tables used to follow it too, so the
  # lane's size depended on which test had run before.
  def test_the_tables_do_not_depend_on_the_stack_in_effect
    sizes = [Exhaustive::STACK_B, Ver("14.4.0")].map { |v|
      Exhaustive.forget_tables!
      pkgmgr.with_host_stack(v) { Exhaustive.tables_for("stack").worlds.length }
    }
    Exhaustive.forget_tables!
    assert_equal sizes.uniq, [sizes.first]
    assert_equal sizes.first, Exhaustive.tables_for("stack").worlds.length
  end

  # Every id the sampler hands out decodes to the case it names.
  def test_ids_round_trip
    for id in Exhaustive.sample_ids(20, seed: 7) do
      assert_equal id, Exhaustive.case_by_id(id).id
    end
  end

  # The bound is two, and the domain says so: no world has three.
  def test_no_world_exceeds_the_bound
    for shape in Exhaustive::SHAPES.keys do
      big = Exhaustive.tables_for(shape).worlds.map(&:length).max
      assert big <= 2, "#{shape}: a world of #{big}"
    end
  end
end
