# SPDX-License-Identifier: BSD-2-Clause
#
# The table of names (table.rb) and the pins kept in one
# (source_pins.rb): what the two grammars accept, what they refuse,
# and that a table is written the same way whoever changed it last.
#
require_relative 'test_helper'
require 'tmpdir'

class TestTable < Minitest::Test

  def test_a_table_is_names_and_values_with_comments_and_blanks
    text = "# what this is\n\nzlib-v1.2.11.tgz: git:abc\n" \
           "gcc-11.5.0.tar.xz:  sha256:def  \n"
    assert_equal({ "zlib-v1.2.11.tgz" => "git:abc",
                   "gcc-11.5.0.tar.xz" => "sha256:def" }, Table.parse(text))
  end

  def test_a_name_has_the_characters_of_a_filename_and_no_other
    for ok in ["a", "libX11-1.8.13.tar.xz", "cpython-3.11.16+2026.tgz",
               "u-boot_2024.04.zip"] do
      assert_equal({ ok => "v" }, Table.parse("#{ok}: v"))
    end
    for bad in ["a b", "a/b", "../x", "", "a:b"] do
      e = assert_raises(Table::Malformed) { Table.parse("#{bad}: v") }
      assert_match(/line 1: /, e.message)
    end
  end

  def test_a_line_without_the_separator_names_its_number
    e = assert_raises(Table::Malformed) {
      Table.parse("a: 1\nb=2\n")
    }
    assert_equal "line 2: no ':' separator", e.message
  end

  def test_a_name_given_twice_is_refused
    e = assert_raises(Table::Malformed) {
      Table.parse("a: 1\n# c\na: 2\n")
    }
    assert_equal "line 3: a given twice", e.message
  end

  def test_render_sorts_by_name_under_the_comment
    text = Table.render({ "b" => "2", "a" => "1" }, comment: "one\ntwo")
    assert_equal "# one\n# two\na: 1\nb: 2\n", text
    assert_equal({ "a" => "1", "b" => "2" }, Table.parse(text))
  end

  def test_write_leaves_a_whole_table_and_no_temporary
    Dir.mktmpdir do |d|
      path = File.join(d, "sub", "t")
      Table.write(path, { "x" => "1" })
      assert_equal({ "x" => "1" }, Table.read(path))
      assert_equal ["t"], Dir.children(File.join(d, "sub"))
      assert_nil Table.read(File.join(d, "none"))
    end
  end
end

class TestSourcePins < Minitest::Test

  SHA = "a" * 64
  COMMIT = "b" * 40

  def test_the_two_kinds_and_their_shapes
    p = SourcePins.parse_pin("sha256:#{SHA}")
    assert_equal :sha256, p.kind
    assert_equal SHA, p.value
    assert_equal "sha256:#{SHA}", p.to_s
    g = SourcePins.parse_pin("git:#{COMMIT}")
    assert_equal :git, g.kind
    assert_equal "git:#{COMMIT}", g.to_s
  end

  def test_what_is_not_a_pin
    for bad in ["sha256:#{'a' * 63}", "sha256:#{'A' * 64}", "git:#{'b' * 7}",
                "git:#{'b' * 64}", "md5:#{'a' * 32}", "sha256", "", nil,
                "sha256:#{SHA} "] do
      assert_nil SourcePins.parse_pin(bad), bad.inspect
    end
  end

  def test_a_file_is_names_to_pins
    pins = SourcePins.parse("# pins\nx.tgz: git:#{COMMIT}\n" \
                            "y.tar.xz: sha256:#{SHA}\n")
    assert_equal :git, pins["x.tgz"].kind
    assert_equal SHA, pins["y.tar.xz"].value
  end

  def test_a_value_that_is_not_a_pin_is_refused_by_name
    e = assert_raises(SourcePins::Malformed) {
      SourcePins.parse("x.tgz: sha256:short\n")
    }
    assert_equal "x.tgz: not a pin: \"sha256:short\"", e.message
    e = assert_raises(SourcePins::Malformed) { SourcePins.parse("x.tgz\n") }
    assert_match(/line 1/, e.message)
  end

  def test_load_says_which_file_and_gives_none_for_no_file
    Dir.mktmpdir do |d|
      assert_equal({}, SourcePins.load(File.join(d, "none")))
      File.write(File.join(d, "p"), "x.tgz: nope\n")
      e = assert_raises(SourcePins::Malformed) {
        SourcePins.load(File.join(d, "p"))
      }
      assert_match(/\/p: x.tgz: not a pin/, e.message)
    end
  end

  def test_the_line_to_paste_and_the_digest_of_a_file
    Dir.mktmpdir do |d|
      f = File.join(d, "z.tar.gz")
      File.write(f, "bytes")
      pin = SourcePins.digest_of(f)
      assert_equal :sha256, pin.kind
      assert_equal Digest::SHA256.hexdigest("bytes"), pin.value
      assert_equal "z.tar.gz: #{pin}", SourcePins.line("z.tar.gz", pin)
    end
    assert_equal "git:#{COMMIT}", SourcePins.commit("#{COMMIT}\n").to_s
    assert_nil SourcePins.commit("abc123")
  end

  def test_the_tree_s_own_file_parses
    pins = SourcePins.load
    refute_empty pins
    assert pins.values.all? { |p| p.is_a?(SourcePins::Pin) }
  end
end

class TestCacheHashes < Minitest::Test
  include TestHelper

  SHA = SourcePins::Pin.new(kind: :sha256, value: "a" * 64)
  GIT = SourcePins::Pin.new(kind: :git, value: "b" * 40)

  def judge(**kw) = Cache::Hashes.judge(**{ recorded: nil, ref: nil }.merge(kw))

  def test_a_file_stands_when_recorded_bytes_and_pin_agree
    assert_equal :ok, judge(pin: SHA, digest: SHA, recorded: SHA.to_s)
    assert_equal :ok, judge(pin: SHA, digest: SHA)
    assert_equal :ok, judge(pin: GIT, digest: SHA, ref: GIT.value)
  end

  def test_bytes_that_moved_since_placement_are_damage_whatever_the_pin
    other = SourcePins::Pin.new(kind: :sha256, value: "c" * 64)
    assert_equal :damaged, judge(pin: SHA, digest: other, recorded: SHA.to_s)
    assert_equal :damaged, judge(pin: nil, digest: other, recorded: SHA.to_s)
    assert_equal :damaged,
                 judge(pin: GIT, digest: other, recorded: SHA.to_s,
                       ref: GIT.value)
  end

  def test_no_pin_is_unpinned_and_an_intact_file_the_pin_does_not_name_is_other
    assert_equal :unpinned, judge(pin: nil, digest: SHA)
    assert_equal :unpinned, judge(pin: nil, digest: SHA, recorded: SHA.to_s)
    other = SourcePins::Pin.new(kind: :sha256, value: "c" * 64)
    assert_equal :other, judge(pin: SHA, digest: other)
    assert_equal :other, judge(pin: GIT, digest: SHA, ref: "c" * 40)
    assert_equal :other, judge(pin: GIT, digest: SHA, ref: nil)
    # A pin of the wrong kind names nothing the file can be.
    assert_equal :other, judge(pin: GIT, digest: SHA, ref: SHA.value)
    # ...and a ref means nothing to a sha256 pin.
    assert_equal :ok, judge(pin: SHA, digest: SHA, ref: GIT.value)
  end

  def test_the_table_records_forgets_and_drops_what_is_gone
    with_fake_tc do |tc|
      FileUtils.touch(tc / "cache" / "a.tgz")
      FileUtils.touch(tc / "cache" / "b.tgz")
      Cache::Hashes.record("a.tgz", SHA)
      Cache::Hashes.record("b.tgz", SHA)
      assert_equal SHA.to_s, Cache::Hashes.of("a.tgz")
      assert_equal "a.tgz: #{SHA}\nb.tgz: #{SHA}\n",
                   File.read(tc / "cache" / ".hashes")
      Cache::Hashes.forget("a.tgz")
      assert_nil Cache::Hashes.of("a.tgz")
      File.delete(tc / "cache" / "b.tgz")
      Cache::Hashes.record("c.tgz", GIT)   # c is not there either
      assert_equal({}, Cache::Hashes.read)
    end
  end

  def test_a_damaged_table_is_dropped_with_a_warning_not_refused
    with_fake_tc do |tc|
      File.write(tc / "cache" / ".hashes", "a.tgz: x\nno separator\n")
      out = capture_output { assert_equal({}, Cache::Hashes.read) }
      assert_match(/\.hashes: line 2: no ':' separator: ignoring the table/,
                   out)
    end
  end
end
