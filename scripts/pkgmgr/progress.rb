# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'

class ProgressReporter
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
    @total = total.to_f
    if @expected_len
      known_length()
    else
      no_length()
    end
  end

  def finish
    puts "" if @tty
  end

  private
  def known_length

    assert { ! @expected_len.nil? }
    ratio = @total / @expected_len
    p = ratio * 1000
    last_p = @last_update / @expected_len * 1000

    if @tty
      rows, cols = IO.console.winsize
    end

    gen_line = ->(pStr) {
      total_MB = ('%.1f' % (1.0 * @total / MB)).rjust(6)
      exp_MB = ('%.1f' % (1.0 * @expected_len / MB)).rjust(6)
      numProgStr = "#{total_MB} / #{exp_MB} MB"
      percStr = '%.1f' % (p/10)
      return "#{numProgStr} #{pStr}[#{percStr.rjust(5)}%]"
    }

    if p - last_p >= 5 || @total == @expected_len

      # Generate the progress line without a progress bar.
      line = gen_line.call("")
      ll = line.length

      if @tty
        rem = [cols - ll, 50].min
        if rem >= 20
          net = rem - (
            1 + # ' ' space after '[===> ]'
            2  # [ and ]
          )

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

          line = gen_line.call(pStr)

          assert { pStr.length + ll == [cols, ll + 50].min }
          assert { line.length <= cols }
        end
      end

      @w.call line
      @last_update = @total
    end
  end

  def no_length
    assert { @expected_len.nil? }
    total_MB = '%.1f' % (1.0 * @total / MB)
    if @total - @last_update >= MB
      @w.call "#{total_MB} MB"
      @last_update = @total
    end
  end
end  # class ProgressReporter

def test_progress_reporter
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


