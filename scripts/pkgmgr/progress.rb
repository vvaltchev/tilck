# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'

class ProgressReporter

  PERC_EPS = 0.5       # Min percentage delta for a new update
  ABS_EPS = MB / 2.0   # Min absolute delta for a new update (expected is nil)

  def initialize(expected_len)

    @expected_len = expected_len
    @total = 0.0        # total progress now
    @last_update = 0.0  # last value of `total` updated on screen

    if STDOUT.tty?
      @tty = true       # are we writing updates to a TTY ?
      @w = ->(x) { print "\r"; print x; STDOUT.flush; }
    else
      @w = ->(x) { puts x; }
      @tty = false
    end
  end

  def update(total)
    assert { !@expected || total <= @expected }
    @total = total.to_f

    if @expected_len
      updated = known_length()
    else
      updated = no_length()
    end

    @last_update = total if updated
  end

  def finish
    puts "" if @tty
  end

  private
  def gen_progress_bar(cols, ll, ratio)

    rem = [cols - ll, 50].min
    net = rem - (
      1 + # ' ' space after '[===> ]'
      2  # [ and ]
    )

    if net < 10
      # Not enough space for a reasonable progress bar. Skip it.
      return nil
    end

    if ratio < 1.0
      arrow = 1
      dashes = [(ratio * net).to_i - arrow, 0].max
    else
      arrow = 0
      dashes = net
    end
    spaces = net - dashes - arrow

    assert { (dashes + spaces + arrow) == net }
    pStr = "[" + "=" * dashes + ">" * arrow + " " * spaces + "] "

    assert { pStr.length + ll == [cols, ll + 50].min }
    return pStr
  end

  def should_show_update
    diff = @total - @last_update
    return true if diff > 0 && @total == @expected_len

    if @expected
      last_p = @last_update / @expected_len * 100
      p = @total / @expected_len * 100
      return true if p - last_p >= PERC_EPS
    else

      return true if diff > ABS_EPS
    end

    return false
  end

  def known_length

    assert { ! @expected_len.nil? }
    ratio = @total / @expected_len
    p = ratio * 100

    if @tty
      rows, cols = IO.console.winsize
    end

    gen_line = ->(pStr) {
      total_MB = ('%.1f' % (1.0 * @total / MB)).rjust(6)
      exp_MB = ('%.1f' % (1.0 * @expected_len / MB)).rjust(6)
      numProgStr = "#{total_MB} / #{exp_MB} MB"
      percStr = ('%.1f' % p).rjust(5)
      return "#{numProgStr} #{pStr}[#{percStr}%]"
    }

    return false unless should_show_update()

    # Generate the progress line without a progress bar.
    line = gen_line.call("")

    if @tty
      progStr = gen_progress_bar(cols, line.length, ratio)
      if progStr
        line = gen_line.call(progStr)
        assert { line.length <= cols }
      end
    end

    @w.call line
    return true
  end

  def no_length
    assert { @expected_len.nil? }
    return false unless should_show_update()

    total_MB = '%.1f' % (1.0 * @total / MB)
    @w.call "#{total_MB} MB"
    return true
  end

end  # class ProgressReporter

def test_progress_reporter_with_length
  exp = 1151 * MB
  tot = 0
  r = ProgressReporter.new(exp)

  while tot < exp
    tot = [tot + (10.0 * rand(0..1) * MB).to_i, exp].min
    r.update(tot)
    sleep 0.01
  end
  r.finish()
end


