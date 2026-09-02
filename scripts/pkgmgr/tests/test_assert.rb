# SPDX-License-Identifier: BSD-2-Clause
#
# assert() must be cheap when it holds and eloquent when it does not.
#
# PowerAssert renders the expression by enabling a TracePoint and
# re-reading the source file, and it was doing that on every call --
# eighty per cent of a plain `-l` was spent inside it, for assertions
# that all passed. It now runs only for a failure, which is the only
# time anyone reads its output.
#
# Both halves are pinned here: dropping PowerAssert altogether would
# make the first test pass and cost the diagnostic, and putting it
# back on the happy path would make the second pass and cost the
# speed.
#

require_relative 'test_helper'
require_relative '../early_logic'

# A plain object, on purpose. `assert` is a private method on Object,
# but Minitest::Assertions defines one too -- so inside a test case,
# and inside anything CALLED from one, the name resolves to Minitest's.
# Only a receiver that is not a test case reaches the package
# manager's.
class AssertProbe

  def holds
    assert { 1 + 1 == 2 }
  end

  # A normal body, not an endless method: PowerAssert reads the source
  # line to place its arrows, and given `def f(a, b) = assert { ... }`
  # it prints the definition and no value tree at all.
  def compares(a, b)
    assert { a.length == b.length }
  end
end

class TestAssert < Minitest::Test

  include TestHelper

  def test_a_holding_assertion_returns_true
    assert_equal true, AssertProbe.new.holds
  end

  # The expensive part must not run on the happy path.
  def test_a_holding_assertion_does_not_render_anything
    calls = 0
    original = PowerAssert.method(:start)

    PowerAssert.define_singleton_method(:start) { |*, **, &blk|
      calls += 1
      true
    }

    begin
      AssertProbe.new.holds
    ensure
      # Put the real one BACK, not just remove the override: start is
      # a module_function, so the override replaced the only
      # definition there was, and removing it would leave the module
      # without one for every test that runs after this.
      PowerAssert.define_singleton_method(:start, original)
    end

    assert_equal 0, calls,
                 "the renderer ran for an assertion that passed"
  end

  # ...and the diagnostic is intact when it does run. This is the
  # whole reason PowerAssert is here.
  def test_a_failing_assertion_shows_the_expression
    err = assert_raises(RuntimeError) { AssertProbe.new.compares("abc", "wxyz") }

    assert_match(/Assertion failed/, err.message)
    assert_match(/a\.length == b\.length/, err.message)
    assert_match(/"abc"/, err.message, "the values are not shown")
    assert_match(/"wxyz"/, err.message)
  end
end
