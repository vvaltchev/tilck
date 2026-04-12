# SPDX-License-Identifier: BSD-2-Clause
#
# Test runner for the Ruby package manager.
#
# Usage:
#   ruby scripts/pkgmgr/tests/run_all.rb [OPTIONS]
#
# Options:
#   --coverage       Enable code coverage + HTML report
#   --filter REGEX   Run only tests whose full name matches REGEX
#   --verbose-tests  Show stdout/stderr even for passing tests
#

# --- Parse runner options before minitest loads ---

$coverage_enabled  = ARGV.delete("--coverage")
$verbose_tests     = ARGV.delete("--verbose-tests")
$test_filter       = nil

if (idx = ARGV.index("--filter"))
  ARGV.delete_at(idx)
  $test_filter = ARGV.delete_at(idx)
end

if $coverage_enabled
  require 'coverage'
  Coverage.start(lines: true)
end

# --- Custom reporter with pretty output ---

require 'minitest'
require 'stringio'

class PrettyReporter < Minitest::AbstractReporter

  # xterm-256 colors
  ESC       = "\e["
  GREEN     = "#{ESC}38;5;40m"
  RED       = "#{ESC}38;5;196m"
  YELLOW    = "#{ESC}38;5;220m"
  CYAN      = "#{ESC}38;5;75m"
  GRAY      = "#{ESC}38;5;245m"
  BOLD      = "#{ESC}1m"
  RESET     = "#{ESC}0m"
  DIM       = "#{ESC}2m"

  STDOUT_PFX = "#{CYAN}stdout#{RESET}#{DIM}│#{RESET} "
  STDERR_PFX = "#{YELLOW}stderr#{RESET}#{DIM}│#{RESET} "
  HLINE      = "#{DIM}#{"─" * 72}#{RESET}"

  def initialize
    super
    @passes = 0
    @fails = []
    @errors = []
    @skips = 0
    @total_time = 0.0
    @total_assertions = 0
    @current_class = nil
  end

  def start
    @wall_start = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    puts HLINE
    puts "#{BOLD}  Ruby pkgmgr test suite#{RESET}"
    puts HLINE
    puts
  end

  def prerecord(klass, name)
    if klass.name != @current_class
      @current_class = klass.name
      puts "  #{DIM}#{klass.name}#{RESET}"
    end
  end

  def record(result)
    @total_time += result.time
    @total_assertions += result.assertions
    full_name = "#{result.class}##{result.name}"

    if result.passed?
      @passes += 1
      ms = "%.0f" % (result.time * 1000)
      print "    #{result.name.ljust(55)} "
      puts "#{GREEN}[ OK ]#{RESET}  #{DIM}#{ms}ms#{RESET}"
      show_captured(result) if $verbose_tests
    elsif result.skipped?
      @skips += 1
      print "    #{result.name.ljust(55)} "
      puts "#{YELLOW}[ SKIP ]#{RESET}"
    else
      if result.failure.is_a?(Minitest::UnexpectedError)
        @errors << result
        print "    #{result.name.ljust(55)} "
        puts "#{RED}[ ERROR ]#{RESET}"
      else
        @fails << result
        print "    #{result.name.ljust(55)} "
        puts "#{RED}[ FAIL ]#{RESET}"
      end
      show_captured(result)
      show_failure(result)
    end
  end

  def report
    wall = Process.clock_gettime(Process::CLOCK_MONOTONIC) - @wall_start
    total = @passes + @fails.length + @errors.length + @skips

    puts
    puts HLINE

    if @fails.empty? && @errors.empty?
      status = "#{GREEN}#{BOLD}ALL PASSED#{RESET}"
    else
      status = "#{RED}#{BOLD}FAILED#{RESET}"
    end

    printf "  %s  %d tests, %d assertions, ", status, total, @total_assertions
    printf "#{GREEN}%d passed#{RESET}", @passes

    if !@fails.empty?
      printf ", #{RED}%d failed#{RESET}", @fails.length
    end
    if !@errors.empty?
      printf ", #{RED}%d errors#{RESET}", @errors.length
    end
    if @skips > 0
      printf ", #{YELLOW}%d skipped#{RESET}", @skips
    end

    printf "  #{DIM}(%.2fs)#{RESET}\n", wall
    puts HLINE
    puts
  end

  def passed?
    @fails.empty? && @errors.empty?
  end

  private

  def show_captured(result)
    key = "#{result.class_name || result.klass}##{result.name}"
    cap = CaptureOutput::CAPTURED[key]
    return if !cap

    stdout = cap[:stdout]
    stderr = cap[:stderr]
    has_output = (stdout && !stdout.empty?) || (stderr && !stderr.empty?)
    return if !has_output

    puts "    #{DIM}┌── captured output ──#{RESET}"

    if stdout && !stdout.empty?
      stdout.each_line { |l| puts "    #{STDOUT_PFX}#{l.chomp}" }
    end
    if stderr && !stderr.empty?
      stderr.each_line { |l| puts "    #{STDERR_PFX}#{l.chomp}" }
    end

    puts "    #{DIM}└────────────────────#{RESET}"
  end

  def show_failure(result)
    msg = result.failure.message
    loc = result.failure.location
    puts "    #{DIM}┌── failure ──#{RESET}"
    puts "    #{DIM}│#{RESET} #{RED}#{msg.gsub("\n", "\n    #{DIM}│#{RESET} ")}#{RESET}"
    puts "    #{DIM}│#{RESET} at: #{loc}" if loc
    puts "    #{DIM}└─────────────#{RESET}"
  end
end

# --- Capture stdout/stderr per test ---
#
# We store captured output in a class-level hash keyed by the test's
# full name (Class#method). The reporter reads from here after each
# test finishes.

module CaptureOutput

  # { "TestFoo#test_bar" => { stdout: "...", stderr: "..." } }
  CAPTURED = {}

  def run
    out = StringIO.new
    err = StringIO.new
    old_out, old_err = $stdout, $stderr
    $stdout, $stderr = out, err

    result = super

    key = "#{self.class}##{self.name}"
    CAPTURED[key] = { stdout: out.string, stderr: err.string }
    return result

  ensure
    $stdout, $stderr = old_out, old_err
  end
end

Minitest::Test.prepend(CaptureOutput)

# --- Configure minitest ---

# Register our reporter as a minitest plugin. Minitest auto-discovers
# methods matching plugin_*_init and calls them during startup.
module Minitest
  def self.plugin_pretty_init(options)
    self.reporter.reporters.clear
    self.reporter.reporters << PrettyReporter.new
  end
end
Minitest.extensions << "pretty"

# Apply --filter if given.
if $test_filter
  ARGV << "-n" << "/#{$test_filter}/"
end

# --- Load all test files ---

Dir.glob(File.join(__dir__, "test_*.rb")).sort.each { |f| require f }

# --- Coverage report (after tests finish) ---

if $coverage_enabled
  require_relative '../coverage_reporter'
  Minitest.after_run {
    CoverageReporter.report(Coverage.result)
  }
end
