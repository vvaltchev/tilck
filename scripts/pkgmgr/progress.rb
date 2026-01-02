# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'

class ProgressReporter

  PERC_EPS = 0.5       # Min percentage delta for a new update
  ABS_EPS = MB / 2.0   # Min absolute delta for a new update (expected is nil)
  TIME_EPS = 1.0       # Max seconds before an update when total changed.

  def initialize(expected_len)

    @expected_len = expected_len
    @total = 0.0        # total progress now
    @last_update = 0.0  # last value of `total` updated on screen
    @update_count = 0
    @last_update_time = nil

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

    if updated
      @last_update = total
      @last_update_time = Time.now()
      @update_count += 1
    end
  end

  def finish
    puts "" if @tty
  end

  def cancel
    if @tty
      puts
    end
    puts "Operation canceled."
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
    delta = @total - @last_update
    return false if delta == 0
    return true if @total == @expected_len
    return true if !@last_update_time
    return true if (Time.now() - @last_update_time) > TIME_EPS

    if @expected
      last_p = @last_update / @expected_len * 100
      p = @total / @expected_len * 100
      return true if p - last_p >= PERC_EPS
    else
      return true if delta > ABS_EPS
    end

    return false
  end

  def gen_moving_line(cols, ll)
    rem = [cols - ll, 50].min
    slider = "<=>"
    sl = slider.length
    net = rem - (
      1 + # space before '[<=>   ]'
      2   # [ and ]
    )

    if net < 10
      # Not enough space for a reasonable progress bar. Skip it.
      return nil
    end

    m = net - sl
    pos = @update_count % (2 * m + 1)
    if pos > m
      pos = (2 * m - pos)
    end

    space1 = pos
    space2 = net - sl - space1

    assert { space1 >= 0 }
    assert { space2 >= 0 }
    assert { net - sl - space1 >= 0 }
    assert { pos.between?(0, net-sl) }
    assert { (space1 + sl + space2) == net }

    return " [" + " " * space1 + slider + " " * space2 + "]"
  end

  def known_length

    assert { ! @expected_len.nil? }
    ratio = @total / @expected_len
    p = ratio * 100

    gen_line = ->(pStr) {
      total_MB = ('%.1f' % (1.0 * @total / MB)).rjust(6)
      exp_MB = ('%.1f' % (1.0 * @expected_len / MB)).rjust(6)
      numProgStr = "#{total_MB} / #{exp_MB} MB"
      percStr = ('%.1f' % p).rjust(5)
      return "Download: #{numProgStr} #{pStr}[#{percStr}%]"
    }

    return false unless should_show_update()

    # Generate the progress line without a progress bar.
    line = gen_line.call("")

    if @tty
      rows, cols = IO.console.winsize
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

    gen_line = ->(pStr) {
      total_MB = ('%.1f' % (1.0 * @total / MB)).rjust(6)
      return "Download: #{total_MB} MB / ???#{pStr}"
    }

    # Generate the progress report line without a moving bar
    line = gen_line.call("")
    if @tty
      rows, cols = IO.console.winsize
      progStr = gen_moving_line(cols, line.length)
      if progStr
        line = gen_line.call(progStr)
        assert { line.length <= cols }
      end
    end

    @w.call line
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

  rescue SignalException, Interrupt
    r.cancel()
end

def test_progress_reporter_no_length
  exp = 1151 * MB
  tot = 0
  r = ProgressReporter.new(nil)

  while tot < exp
    tot = [tot + (1 * rand(0..1) * KB).to_i, exp].min
    r.update(tot)
    sleep 0.01
  end
  r.finish()

  rescue SignalException, Interrupt
    r.cancel()
end


