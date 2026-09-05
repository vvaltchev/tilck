# SPDX-License-Identifier: BSD-2-Clause
#
# HOW A COMMAND LINE IS WRITTEN DOWN.
#
# Two places show one to a person: the "Run:" line of every build step,
# and the hint that names the system packages to install. Both are read
# far more often than they are pasted, and both have to survive being
# pasted, so the rule is minimal quoting -- a word is left alone unless
# a shell would read it as more than itself.
#
# The `=` is what this exists for. Shellwords.escape treats it as
# unsafe, and printed `make V\=1 -j20` for a command nobody would type
# that way.
#

require_relative 'test_helper'

class TestCmdToS < Minitest::Test

  def test_an_ordinary_command_line_is_left_alone
    assert_equal "make V=1 -j20", cmd_to_s(["make", "V=1", "-j20"])
  end

  def test_the_characters_a_configure_line_is_made_of
    argv = ["./configure", "--prefix=/", "--cpu=i386",
            "--cross-prefix=i686-linux-", "--extra-ldflags=-static",
            "--crtprefix=/lib/i686-tilck-musl"]

    assert_equal argv.join(" "), cmd_to_s(argv)
  end

  def test_a_word_with_a_space_is_quoted_as_one_word
    assert_equal "apt install 'a b'", cmd_to_s(["apt", "install", "a b"])
  end

  # Everything a shell would act on rather than pass through.
  def test_the_words_that_must_be_quoted
    {
      "a b"           => "'a b'",
      "a\tb"          => "'a\tb'",
      ""              => "''",
      "~/x"           => "'~/x'",       # tilde expansion
      "*.c"           => "'*.c'",       # globbing
      "a;b"           => "'a;b'",       # command separator
      "$HOME"         => "'$HOME'",     # variable
      "`id`"          => "'`id`'",      # substitution
      "a|b"           => "'a|b'",
      "a>b"           => "'a>b'",
      "a&b"           => "'a&b'",
      "a#b"           => "'a#b'",
      "-DX=\\\"v\\\"" => "'-DX=\\\"v\\\"'",
    }.each { |word, quoted|
      assert_equal quoted, shell_word(word), "shell_word(#{word.inspect})"
    }
  end

  # The one character single quotes cannot carry: close, escape, open.
  def test_a_single_quote_inside_a_word
    assert_equal %q{'it'\''s'}, shell_word("it's")
  end

  # What the quoting is for: a shell reading the rendered line back
  # gets the argv that went in, one word per word.
  def test_a_shell_reads_back_what_was_written
    argv = ["echo", "a b", "it's", "$HOME", "*", "V=1", "", "~/x"]
    out = `/bin/sh -c #{Shellwords.escape("printf '%s\\n' " + cmd_to_s(argv))}`

    assert_equal argv, out.split("\n", -1)[0...argv.length]
  end
end
