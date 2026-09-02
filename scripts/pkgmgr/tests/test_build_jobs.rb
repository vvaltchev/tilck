# SPDX-License-Identifier: BSD-2-Clause
#
# How many jobs a parallel build may use.
#
# `-j` with no number is not "one per core", it is unlimited. Every
# recipe ran that way, because BUILD_PAR was empty when unset. GCC
# will take hundreds of jobs given the chance.
#
# The arithmetic takes cores and RAM as arguments precisely so it can
# be checked at shapes this machine does not have.
#

require_relative 'test_helper'

class TestBuildJobs < Minitest::Test

  GB = 1024 * 1024 * 1024

  def jobs(cores, gb) = BuildJobs.compute(cores: cores, ram: gb * GB)

  # The worked example: 24 cores, 96 GB. Four cores left free gives 20;
  # five sixths of 96 GB at 4 GB a job gives 20.
  def test_the_worked_example
    assert_equal 20, jobs(24, 96)
  end

  # Memory is the binding constraint on a box with many cores and
  # little RAM -- the case that matters, since it is the one that
  # swaps rather than merely running hot.
  def test_ram_can_be_the_lower_ceiling
    assert_equal 13, jobs(128, 64)
  end

  # ...and cores on a box with the opposite shape.
  def test_cpu_can_be_the_lower_ceiling
    assert_equal 7, jobs(8, 512)
  end

  # A sixth of the cores stays free, always.
  def test_some_cores_are_always_left_free
    for c in [8, 12, 24, 48, 128] do
      assert_operator jobs(c, 4096), :<, c, "#{c} cores: nothing left free"
    end
  end

  # Small machines still get a usable answer rather than zero.
  def test_small_machines_get_at_least_one_job
    assert_operator jobs(1, 1), :>=, 1
    assert_operator jobs(2, 2), :>=, 1
    assert_operator jobs(4, 4), :>=, 1
  end

  # Not knowing the memory costs the RAM ceiling, not the whole
  # calculation.
  def test_unknown_ram_falls_back_to_the_cpu_ceiling
    assert_equal 20, BuildJobs.compute(cores: 24, ram: nil)
    assert_equal 20, BuildJobs.compute(cores: 24, ram: 0)
  end

  # Never unlimited, whatever the shape: that is the whole point.
  def test_the_answer_is_always_a_number
    for c in [1, 2, 4, 24, 256] do
      for g in [1, 4, 96, 1024] do
        n = jobs(c, g)
        assert_kind_of Integer, n
        assert_operator n, :>=, 1, "#{c} cores / #{g} GB gave #{n}"
      end
    end
  end

  # BUILD_PAR is a number now, not an empty string, so the recipes
  # that interpolate it produce -jN rather than a bare -j.
  def test_build_par_is_a_concrete_number
    assert_match(/\A[0-9]+\z/, BUILD_PAR.to_s,
                 "BUILD_PAR is #{BUILD_PAR.inspect}: -j#{BUILD_PAR} " \
                 "would be unlimited")
    assert_operator BUILD_PAR.to_i, :>=, 1
  end

  # This machine's own memory has to be readable, or the ceiling that
  # matters most is silently absent here.
  def test_this_machine_reports_its_memory
    refute_nil BuildJobs.total_ram
    assert_operator BuildJobs.total_ram, :>, GB
  end
end
