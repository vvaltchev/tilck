# SPDX-License-Identifier: BSD-2-Clause
#
# $SRC_REF: naming the source's git ref without reading it.
#
# The value lives in .ref_short, written beside the extracted tree by
# the cache when it clones. A recipe cannot READ it, because
# build_steps is asked for during a staleness check as well, when no
# source tree exists -- a recipe that read it would hash differently
# depending on where the question was asked from. So it is named, and
# resolved at build time only.
#

require_relative 'test_helper'
require_relative '../package'

class TestSrcRefToken < Minitest::Test

  include TestHelper

  def with_src(ref)
    Dir.mktmpdir("pkgmgr-srcref-") do |dir|
      d = Pathname.new(dir)
      File.write(d / ".ref_short", ref + "\n") if ref
      yield d
    end
  end

  def test_it_resolves_to_the_recorded_ref
    with_fake_tc do
      pkg = FakePackage.new("foo")
      with_src("deadbee") do |d|
        assert_equal "gh=deadbee", pkg.expand_tokens("gh=$SRC_REF", d)
      end
    end
  end

  # Trailing newline and surrounding whitespace are the cache's, not
  # the value's.
  def test_the_value_is_stripped
    with_fake_tc do
      pkg = FakePackage.new("foo")
      with_src("  abc123  ") do |d|
        assert_equal "abc123", pkg.expand_tokens("$SRC_REF", d)
      end
    end
  end

  # A package that asks for it and has no git source is asking for
  # something that does not exist. Saying so beats handing back "".
  def test_a_missing_ref_is_refused
    with_fake_tc do
      pkg = FakePackage.new("foo")
      with_src(nil) do |d|
        err = assert_raises(RuntimeError) {
          pkg.expand_tokens("$SRC_REF", d)
        }
        assert_match(/\.ref_short/, err.message)
      end
    end
  end

  # ...and a recipe that never mentions it must not need one, or every
  # non-git package would fail.
  def test_a_recipe_without_the_token_needs_no_source
    with_fake_tc do
      pkg = FakePackage.new("foo")
      with_src(nil) do |d|
        out = pkg.expand_tokens("make -j$PAR $INSTALL", d)
        refute_includes out, "$PAR"
        refute_includes out, "$INSTALL"
      end
    end
  end
end
