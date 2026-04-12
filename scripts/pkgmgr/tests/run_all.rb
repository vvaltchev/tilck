# SPDX-License-Identifier: BSD-2-Clause
#
# Test runner for the Ruby package manager.
#
# Usage:
#   ruby scripts/pkgmgr/tests/run_all.rb              # run tests
#   ruby scripts/pkgmgr/tests/run_all.rb --coverage    # run + HTML report
#

$coverage_enabled = ARGV.delete("--coverage")

if $coverage_enabled
  require 'coverage'
  Coverage.start(lines: true)
end

# Load all test files in this directory.
Dir.glob(File.join(__dir__, "test_*.rb")).sort.each { |f| require f }

if $coverage_enabled
  require_relative '../coverage_reporter'
  Minitest.after_run {
    CoverageReporter.report(Coverage.result)
  }
end
