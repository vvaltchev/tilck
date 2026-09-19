# SPDX-License-Identifier: BSD-2-Clause
#
# THE EXHAUSTIVE LANE, SAMPLED.
#
# The full enumeration (tests/exhaustive/) is some two million cases
# and belongs to CI: `-t --exhaustive`, minutes. The default suite
# runs a fixed-seed sample of it here, so that every local run still
# asks the model five thousand questions the tests did not think of,
# in under a second.
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

  SAMPLE = 5000
  DEFAULT_SEED = 20260903

  def seed = ($exhaustive_seed || DEFAULT_SEED).to_i

  def test_the_instrument_passes_its_self_test
    assert_empty Exhaustive.self_test
  end

  def test_a_sample_of_every_shape_agrees_with_the_model
    ids = $exhaustive_case ? [$exhaustive_case] : nil
    failed = Exhaustive.sample_problems(SAMPLE, seed: seed, ids: ids)

    assert_empty failed,
                 "#{failed.length} of #{ids&.length || SAMPLE} cases " \
                 "disagree with the model (seed #{seed}; replay one " \
                 "with --case ID):\n\n" +
                 failed.first(5).map(&:to_s).join("\n\n")
  end

  # The tables are the same whatever stack the process is in when they
  # are first asked for: the stack compiler's default version follows
  # the stack in effect, and the tables used to follow it too, so the
  # lane's size depended on which test had run before.
  def test_the_tables_do_not_depend_on_the_stack_in_effect
    sizes = [Exhaustive::STACK_B, Ver("14.4.0")].map { |v|
      Exhaustive.forget_tables!
      Exhaustive.harness.with_host_stack(v) {
        Exhaustive.tables_for("stack").worlds.length
      }
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

  # The bound is what the domain says it is, and it is reached: the
  # shape the bugs needed two of has worlds of three.
  def test_no_world_exceeds_the_bound
    for shape in Exhaustive::SHAPES.keys do
      big = Exhaustive.tables_for(shape).worlds.map(&:length).max
      assert big <= Exhaustive::BOUND, "#{shape}: a world of #{big}"
    end
    assert_equal Exhaustive::BOUND,
                 Exhaustive.tables_for("target_2v").worlds.map(&:length).max
  end
end
