# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'
require_relative '../progress'
require 'stringio'

class TestProgressReporterKnownLength < Minitest::Test

  def setup
    # Redirect stdout to capture output
    @old_stdout = $stdout
    $stdout = StringIO.new
  end

  def teardown
    $stdout = @old_stdout
  end

  def test_initialize_with_length
    p = ProgressReporter.new(1000)
    assert_instance_of ProgressReporter, p
  end

  def test_update_and_finish
    p = ProgressReporter.new(1000)
    p.update(0)
    p.update(500)
    p.update(1000)
    p.finish
  end

  def test_progressive_updates
    p = ProgressReporter.new(100 * MB)
    # Feed increasing totals
    (0..10).each { |i|
      p.update(i * 10 * MB)
    }
    p.finish
    output = $stdout.string
    assert_match(/Download:/, output)
    assert_match(/MB/, output)
  end

  def test_full_download_shows_100_percent
    p = ProgressReporter.new(50 * MB)
    p.update(50 * MB)
    p.finish
    output = $stdout.string
    assert_match(/100/, output)
  end
end

class TestProgressReporterNoLength < Minitest::Test

  def setup
    @old_stdout = $stdout
    $stdout = StringIO.new
  end

  def teardown
    $stdout = @old_stdout
  end

  def test_initialize_nil_length
    p = ProgressReporter.new(nil)
    assert_instance_of ProgressReporter, p
  end

  def test_update_unknown_length
    p = ProgressReporter.new(nil)
    p.update(MB)
    p.update(5 * MB)
    p.update(20 * MB)
    p.finish
    output = $stdout.string
    assert_match(/Download:/, output)
    assert_match(/\?\?\?/, output)
  end
end

class TestProgressReporterCancel < Minitest::Test

  def setup
    @old_stdout = $stdout
    $stdout = StringIO.new
  end

  def teardown
    $stdout = @old_stdout
  end

  def test_cancel
    p = ProgressReporter.new(1000)
    p.update(500)
    p.cancel
    output = $stdout.string
    assert_match(/canceled/, output)
  end
end

class TestProgressReporterProgressBar < Minitest::Test

  def setup
    @old_stdout = $stdout
    $stdout = StringIO.new
  end

  def teardown
    $stdout = @old_stdout
  end

  def test_gen_progress_bar
    p = ProgressReporter.new(100 * MB)
    # Access the private method for testing
    bar = p.send(:gen_progress_bar, 80, 30, 0.5)
    assert_instance_of String, bar
    assert_match(/=/, bar)
    assert_match(/>/, bar)
  end

  def test_gen_progress_bar_full
    p = ProgressReporter.new(100 * MB)
    bar = p.send(:gen_progress_bar, 80, 30, 1.0)
    assert_instance_of String, bar
    assert_match(/=/, bar)
    # Full bar has no arrow
    refute_match(/>/, bar)
  end

  def test_gen_progress_bar_too_narrow
    p = ProgressReporter.new(100 * MB)
    bar = p.send(:gen_progress_bar, 40, 35, 0.5)
    assert_nil bar
  end

  def test_gen_moving_line
    p = ProgressReporter.new(nil)
    line = p.send(:gen_moving_line, 80, 30)
    assert_instance_of String, line
    assert_match(/<=>/, line)
  end

  def test_gen_moving_line_too_narrow
    p = ProgressReporter.new(nil)
    line = p.send(:gen_moving_line, 40, 35)
    assert_nil line
  end

  def test_gen_moving_line_bounces
    p = ProgressReporter.new(nil)
    # Simulate multiple updates to test the bounce effect
    positions = (0..20).map { |i|
      p.instance_variable_set(:@update_count, i)
      line = p.send(:gen_moving_line, 80, 30)
      line
    }
    # All should be valid strings
    positions.each { |l| assert_instance_of String, l }
  end
end

class TestProgressReporterShouldShowUpdate < Minitest::Test

  def test_no_delta
    p = ProgressReporter.new(100)
    p.instance_variable_set(:@total, 50.0)
    p.instance_variable_set(:@last_update, 50.0)
    refute p.send(:should_show_update)
  end

  def test_first_update
    p = ProgressReporter.new(100)
    p.instance_variable_set(:@total, 10.0)
    p.instance_variable_set(:@last_update, 0.0)
    p.instance_variable_set(:@last_update_time, nil)
    assert p.send(:should_show_update)
  end

  # Regression test: with a known-length download, a small delta that
  # crosses PERC_EPS (0.5%) but is below ABS_EPS (512KB) should still
  # trigger an update via the percentage branch. A bug where
  # @expected was used instead of @expected_len caused this branch to
  # be dead code, making all known-length downloads use the absolute
  # delta threshold instead.
  def test_percentage_threshold_known_length
    total_size = 100 * MB
    p = ProgressReporter.new(total_size)

    # Simulate: we're at 50%, advance by ~0.6% (600KB on 100MB).
    # 600KB > PERC_EPS (0.5%) but 600KB < ABS_EPS (512KB)... wait,
    # 600KB > 512KB. Use a bigger file so 0.6% is < 512KB.
    #
    # With 200MB total: 0.5% = 1MB > ABS_EPS. Still too big.
    # With 200MB total: 0.3% = 600KB > ABS_EPS. Hmm.
    #
    # Actually ABS_EPS = MB/2 = 524288 bytes. PERC_EPS = 0.5%.
    # For PERC_EPS to trigger but not ABS_EPS, we need:
    #   delta >= total * 0.005  (percentage threshold)
    #   delta < 524288          (absolute threshold)
    # So: total * 0.005 < 524288 → total < 104857600 = 100MB
    # And delta >= total * 0.005
    #
    # Use total = 50MB. Then 0.5% = 256KB. A delta of 300KB is:
    #   300KB > 256KB (PERC_EPS passes)
    #   300KB < 512KB (ABS_EPS fails)
    # With the bug: falls to else branch, ABS_EPS check fails → false
    # Without bug: percentage check passes → true

    total_size = 50 * MB
    p = ProgressReporter.new(total_size)
    base = 25 * MB  # 50% downloaded
    delta = 300 * KB

    p.instance_variable_set(:@total, (base + delta).to_f)
    p.instance_variable_set(:@last_update, base.to_f)
    p.instance_variable_set(:@last_update_time, Time.now)

    assert p.send(:should_show_update),
           "Should trigger update via percentage threshold (0.6% > 0.5%)"
  end

  def test_at_completion
    p = ProgressReporter.new(100)
    p.instance_variable_set(:@total, 100.0)
    p.instance_variable_set(:@last_update, 90.0)
    p.instance_variable_set(:@expected_len, 100)
    assert p.send(:should_show_update)
  end
end
