# SPDX-License-Identifier: BSD-2-Clause
#
# Test runner for the Ruby package manager.
#
# Usage:
#   ruby scripts/pkgmgr/tests/run_all.rb [OPTIONS]
#
# Options:
#   --coverage            Collect code coverage + HTML report
#   --filter REGEX        Run only tests whose name matches REGEX
#   --verbose-tests       Show stdout/stderr even for passing tests
#   --system-tests        After unit tests: install all pkgs, build all archs
#   --all-build-types     With --system-tests: run all build generator configs
#   --run-also-tilck-tests  With --system-tests: run gtests + system tests
#

# --- Parse runner options before minitest loads ---

$coverage_enabled    = ARGV.delete("--coverage")
$verbose_tests       = ARGV.delete("--verbose-tests")
$dry_run             = ARGV.delete("--dry-run")
$system_tests        = ARGV.delete("--system-tests")
$all_build_types     = ARGV.delete("--all-build-types")
$run_tilck_tests     = ARGV.delete("--run-also-tilck-tests")
$test_filter         = nil
$test_arch           = nil
$test_packages_filter = nil

if (idx = ARGV.index("--filter"))
  ARGV.delete_at(idx)
  $test_filter = ARGV.delete_at(idx)
end

if (idx = ARGV.index("--test-arch"))
  ARGV.delete_at(idx)
  $test_arch = ARGV.delete_at(idx)
end

if (idx = ARGV.index("--test-packages-filter"))
  ARGV.delete_at(idx)
  $test_packages_filter = ARGV.delete_at(idx)
end

if $coverage_enabled
  require 'coverage'
  require 'json'
  # Branches too: a line counts as covered the first time it runs,
  # with only one of its two outcomes ever having happened, and that
  # is exactly where this suite has been missing bugs.
  Coverage.start(lines: true, branches: true)

  # Tell subprocesses (build_toolchain invocations) to also collect
  # coverage. Each subprocess writes a JSON file; we merge them all
  # at the end.
  $coverage_dir = File.join(__dir__, "..", "..", "..", "coverage_html")
  ENV["COVERAGE_DIR"] = $coverage_dir
end

# --- Custom reporter with pretty output ---

require 'minitest'
require 'stringio'
require_relative '../term'

# Global flag set by PrettyReporter after tests complete.
$unit_tests_passed = false

class PrettyReporter < Minitest::AbstractReporter

  include Term

  STDOUT_PFX = "#{CYAN256}stdout#{RESET}#{DIM}│#{RESET} "
  STDERR_PFX = "#{YELLOW256}stderr#{RESET}#{DIM}│#{RESET} "

  def initialize
    super
    @passes = 0
    @fails = []
    @errors = []
    @skips = []
    @total_time = 0.0
    @total_assertions = 0
    @current_class = nil
    @abort = false
  end

  def start
    @wall_start = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    puts HLINE
    puts "#{BOLD}  Ruby pkgmgr test suite#{RESET}"
    puts HLINE
    puts
  end

  def prerecord(klass, name)
    return if @abort
    if klass.name != @current_class
      @current_class = klass.name
      puts "  #{DIM}#{klass.name}#{RESET}"
    end
  end

  def record(result)
    @total_time += result.time
    @total_assertions += result.assertions

    if result.passed?
      @passes += 1
      ms = "%.0f" % (result.time * 1000)
      print "    #{result.name.ljust(55)} "
      puts "#{GREEN256}[ OK ]#{RESET}  #{DIM}#{ms}ms#{RESET}"
      show_captured(result) if $verbose_tests
    elsif result.skipped?
      @skips << result
      print "    #{result.name.ljust(55)} "
      print "#{YELLOW256}[ SKIP ]#{RESET}"
      why = skip_reason(result)
      print "  #{DIM}#{why}#{RESET}" if why
      puts
    else
      if result.failure.is_a?(Minitest::UnexpectedError)
        @errors << result
        print "    #{result.name.ljust(55)} "
        puts "#{RED256}[ ERROR ]#{RESET}"
      else
        @fails << result
        print "    #{result.name.ljust(55)} "
        puts "#{RED256}[ FAIL ]#{RESET}"
      end
      show_captured(result)
      show_failure(result)

      # Stop on first failure
      @abort = true
      Minitest::Runnable.runnables.clear
    end
  end

  def report
    wall = Process.clock_gettime(Process::CLOCK_MONOTONIC) - @wall_start
    total = @passes + @fails.length + @errors.length + @skips.length

    puts
    puts HLINE

    if @fails.empty? && @errors.empty?
      status = "#{GREEN256}#{BOLD}ALL PASSED#{RESET}"
    else
      status = "#{RED256}#{BOLD}FAILED#{RESET}"
    end

    printf "  %s  %d tests, %d assertions, ", status, total, @total_assertions
    printf "#{GREEN256}%d passed#{RESET}", @passes

    if !@fails.empty?
      printf ", #{RED256}%d failed#{RESET}", @fails.length
    end
    if !@errors.empty?
      printf ", #{RED256}%d errors#{RESET}", @errors.length
    end
    if !@skips.empty?
      printf ", #{YELLOW256}%d skipped#{RESET}", @skips.length
    end

    printf "  #{DIM}(%.2fs)#{RESET}\n", wall
    puts HLINE
    show_skips
    puts
  end

  def passed?
    ok = @fails.empty? && @errors.empty?
    $unit_tests_passed = ok
    ok
  end

  private

  # What a skip actually says. Minitest fills in a placeholder when
  # `skip` is called with no argument, which is no more informative
  # than the word SKIP itself, so it is dropped rather than echoed.
  def skip_reason(result)
    msg = result.failure&.message
    return nil if msg.nil? || msg.empty?
    return nil if msg.start_with?("Skipped, no message given")
    return msg.lines.first.chomp
  end

  # A skipped test is a check that did NOT run, and a suite that
  # prints ALL PASSED above a silent skip is claiming more than it
  # verified. Repeat them under the summary, with their reasons, so
  # that a CI log shows what went unchecked at the point where
  # someone reads the result.
  def show_skips
    return if @skips.empty?

    puts
    puts "  #{YELLOW256}#{@skips.length} skipped#{RESET} " \
         "#{DIM}(checks that did not run)#{RESET}"

    for r in @skips do
      why = skip_reason(r) || "no reason given"
      puts "    #{DIM}-#{RESET} #{r.name}: #{DIM}#{why}#{RESET}"
    end
  end

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
    puts "    #{DIM}│#{RESET} #{RED256}#{msg.gsub("\n", "\n    #{DIM}│#{RESET} ")}#{RESET}"
    puts "    #{DIM}│#{RESET} at: #{loc}" if loc
    puts "    #{DIM}└─────────────#{RESET}"
  end
end

# --- Capture stdout/stderr per test ---

module CaptureOutput

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

# --- Dry-run: list tests without running, then continue to system tests ---

if $dry_run
  DRY_TAG = "#{Term::CYAN256}[ DRY ]#{Term::RESET}"

  puts Term::HLINE
  puts "#{Term::BOLD}  Ruby pkgmgr test suite#{Term::RESET}"
  puts Term::HLINE
  puts

  count = 0
  Minitest::Runnable.runnables.sort_by(&:name).each { |klass|
    methods = klass.instance_methods(false).grep(/\Atest_/).sort
    next if methods.empty?
    puts "  #{Term::DIM}#{klass.name}#{Term::RESET}"
    methods.each { |m|
      print "    #{m.to_s.ljust(55)} "
      puts DRY_TAG
      count += 1
    }
  }

  puts
  puts Term::HLINE
  puts "  #{count} unit tests  #{DRY_TAG}"
  puts Term::HLINE
  puts

  # Don't run minitest — clear runnables and mark as passed.
  Minitest::Runnable.runnables.clear
  $unit_tests_passed = true
end

# --- After minitest finishes: system tests + coverage ---

Minitest.after_run {

  # What the laws saw. A suite whose command lines all fell outside
  # the model's grammar would pass while checking nothing; this line
  # is how that would be noticed.
  if defined?(Laws)
    puts "  #{Term::DIM}laws: #{Laws.checked} command lines judged " \
         "against the model, #{Laws.unparsed.length} outside its " \
         "grammar#{Term::RESET}"
    puts
  end

  if $unit_tests_passed && $system_tests
    require_relative 'system_tests'

    SystemTests.run(
      run_tilck: $run_tilck_tests,
      all_build_types: !!$all_build_types,
      arch: $test_arch,
      packages_filter: $test_packages_filter
    )
  end

  if $coverage_enabled
    require_relative '../coverage_reporter'

    # Merge this process's coverage with any subprocess coverage
    # files (written by build_toolchain invocations during system
    # tests). Each subprocess writes coverage_<pid>.json.
    raw = Coverage.result

    # Coverage.result returns frozen arrays — dup everything so we
    # can merge subprocess data into it.
    merged = {}
    raw.each { |path, data|
      merged[path] = { lines: data[:lines].dup }

      # Branch data is NOT merged from subprocesses. Its keys are
      # arrays that JSON can only carry as strings, and the subprocess
      # runs are system tests -- real installs -- while the branches
      # worth counting are the ones the unit tests reach. Merging a
      # stringified half would inflate the number without making it
      # mean anything.
      merged[path][:branches] = data[:branches] if data[:branches]
    }

    Dir.glob(File.join($coverage_dir, "coverage_*.json")).each { |f|
      begin
        sub = JSON.parse(File.read(f))
        sub.each { |path, data|
          lines = data["lines"]
          next if !lines

          if merged[path]
            # Merge: for each line, take the max of both counts.
            mine = merged[path][:lines]
            lines.each_with_index { |count, i|
              next if count.nil?
              if mine[i].nil?
                mine[i] = count
              else
                mine[i] = [mine[i], count].max
              end
            }
          else
            merged[path] = { lines: lines }
          end
        }
        File.delete(f)
      rescue => e
        $stderr.puts "WARNING: failed to merge #{f}: #{e}"
      end
    }

    CoverageReporter.report(merged)
  end

  if !$unit_tests_passed
    exit 1
  end
}
