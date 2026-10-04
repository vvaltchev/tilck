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

  # --- the full lane's plumbing: parts in processes, talking in files ---

  # The lane reads a part's progress while the part goes on writing it,
  # so a reader that opened the file just before an update reads the
  # whole report it opened. Rewritten in place, the file could be read
  # between the truncation and the write: the lane read nothing, and
  # died on it on CI.
  def test_a_progress_report_is_replaced_never_rewritten
    Dir.mktmpdir do |dir|
      f = File.join(dir, "shape.0.progress")
      Exhaustive.write_progress(f, 500, 0, 1.5)
      File.open(f) { |held|
        Exhaustive.write_progress(f, 1000, 1, 3.0)
        assert_equal "500 0 1.5", held.read
      }
      assert_equal Exhaustive::Progress.new(1000, 1, 3.0),
                   Exhaustive.read_progress(f)
      assert_equal ["shape.0.progress"], Dir.children(dir)
    end
  end

  # No report yet is nil; anything but a whole one is refused, never
  # read as zero.
  def test_a_progress_report_is_read_whole_or_refused
    Dir.mktmpdir do |dir|
      f = File.join(dir, "shape.0.progress")
      assert_nil Exhaustive.read_progress(f)
      for bad in ["", "500 0", "500 x 1.5", "500 0 1.5 7"] do
        File.write(f, bad)
        assert_raises(RuntimeError, ArgumentError, bad.inspect) {
          Exhaustive.read_progress(f)
        }
      end
    end
  end

  # The full lane over the smallest shapes, one per entry of `parts`:
  # each shape's work is stood in for by its entry (called with
  # run_shape's arguments), the self-test and the fake toolchain left
  # out. What is judged is how the parts' processes and the lane talk.
  # Returns [the shapes, run_all's answer, its output].
  def lane_with_parts(*parts)
    shapes = Exhaustive::SHAPES.keys.min_by(parts.length) { |s|
      Exhaustive.count(s)
    }
    work = shapes.zip(parts).to_h
    run = ->(sh, **kw) { work.fetch(sh).call(sh, **kw) }
    ok = nil
    out, = capture_io {
      Exhaustive.stub(:self_test, []) {
        Exhaustive.stub(:in_lane, ->(&blk) { blk.call }) {
          Exhaustive.stub(:run_shape, run) {
            ok = Exhaustive.run_all(shapes: shapes, jobs: shapes.length)
          }
        }
      }
    }
    return [shapes, ok, out]
  end

  def test_the_lane_sums_what_its_parts_publish
    shapes, ok, out = lane_with_parts(->(sh, progress:, **) {
      progress.call(500, 1, 0.5)
      [Exhaustive::Summary.new(sh, 7, 0, 0.5), []]
    })
    assert ok, out
    assert_match(/^  #{shapes[0]}\s+7 cases\s+0 failed\s+0\.5s$/, out)
  end

  # A part that dies -- raising, or killed before it can say anything
  # -- has published nothing: the lane names it and fails, instead of
  # dying itself on a result that is not there.
  def test_a_part_that_dies_fails_the_lane_by_name
    shapes, ok, out = lane_with_parts(
      ->(*, **) { raise "a bug in a part" },
      ->(*, **) {
        Process.kill(:KILL, Process.pid)
        sleep
      }
    )
    died = ->(i, why) {
      /^  #{shapes[i]}\s+part 1\/1 died \(#{Regexp.escape(why)}\)/
    }
    refute ok
    assert_match died.(0, "exit status 1"), out
    assert_match died.(1, "killed by signal #{Signal.list["KILL"]}"), out
  end
end
