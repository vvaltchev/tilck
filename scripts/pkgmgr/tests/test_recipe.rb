# SPDX-License-Identifier: BSD-2-Clause
#
# THE RECIPE MODEL, and the encoding underneath it.
#
# A digest is an instrument: everything downstream of it -- every
# listing, every staleness check, every refusal to rebuild -- is right
# only if it is. So it is tested the way an instrument is tested,
# rather than by exercising the code that happens to call it:
#
#   * the canonical bytes of a known recipe are written out BY HAND
#     here, from the encoding's definition. If the two disagree, one
#     of them is wrong and the test says which byte;
#
#   * a golden digest locks the whole pipeline, so that a change to
#     the encoding cannot happen by accident. Updating the constant is
#     the moment to bump Recipe::FORMAT;
#
#   * nothing may collide. ["a", "b"] and ["ab"] are different
#     recipes and must hash differently, and so must a step moved,
#     reordered, or lifted out of a Within;
#
#   * the evolution contract is a test, not a promise: adding an
#     optional field to a kind, and adding a whole new kind, must
#     leave every existing digest untouched.
#
# The execution tests are the other half: a model that hashes
# beautifully and does the wrong thing is worse than no model.
#

require 'tmpdir'
require_relative 'test_helper'
require_relative '../recipe'

#
# The encoding, byte for byte.
#
class TestRecipeCanon < Minitest::Test

  include Recipe::DSL

  # The recipe the hand-written bytes below describe.
  def sample
    return [
      Mkdir(path: "build"),
      Within(dir: "build", steps: [
        Run(argv: ["../configure", "--prefix=$PREFIX"]),
        Run(argv: ["make", "-j$PAR"]),
      ]),
    ]
  end

  # Derived from the definition of canon, not from its output:
  # every string is "s<bytes>:<bytes>", every array "a<n>:", every
  # hash "h<n>:" with its pairs in key order, and every step "S"
  # followed by its tag and its identity map.
  SAMPLE_CANON =
    "a2:" +                                      # two steps, in order
      "Ss5:mkdir" +                              #   Mkdir
        "h1:s4:paths5:build" +                   #     path="build"
      "Ss6:within" +                             #   Within
        "h2:" +                                  #     two fields set:
          "s3:dirs5:build" +                     #       dir="build"
          "s5:steps" +                           #       steps=
            "a2:" +                              #         two, in order
              "Ss3:runh1:s4:argv" +              #           Run
                "a2:s12:../configures16:--prefix=$PREFIX" +
              "Ss3:runh1:s4:argv" +              #           Run
                "a2:s4:makes6:-j$PAR"

  # env: {} and unset: [] are at their defaults on that Within, and
  # stdin/status are on both Runs: none of them appears above. That
  # omission is the whole evolution contract.
  def test_canon_is_what_the_encoding_says
    assert_equal SAMPLE_CANON, Recipe.canon(sample)
  end

  # Changing this constant means every install in the world reads
  # stale. Bump Recipe::FORMAT in the same commit, deliberately.
  GOLDEN = "sha256:22734a120911559e57fdb63ced58532c"

  def test_golden_digest
    assert_equal GOLDEN, Recipe.digest(sample)
  end

  def test_the_encoding_is_deterministic
    assert_equal Recipe.canon(sample), Recipe.canon(sample)
    assert_equal Recipe.digest(sample), Recipe.digest(sample)
  end

  #
  # Ambiguity is the failure mode a digest dies of: two different
  # things that serialise to the same bytes. Length prefixes are what
  # prevent it, and these are the cases that would collide without
  # them.
  #
  def test_nothing_collides

    distinct = [
      ["a", "b"],
      ["ab"],
      [["a"], ["b"]],
      [["a", "b"]],
      ["a:b"],
      ["a", ":b"],
      [""],
      [],
      ["1"],
      [1],
      [:a],
      ["a"],
      [nil],
      [false],
    ]

    seen = {}

    for v in distinct do
      c = Recipe.canon(v)
      assert_nil seen[c], "#{v.inspect} and #{seen[c].inspect} collide"
      seen[c] = v
    end
  end

  def test_order_matters
    a = [Mkdir(path: "one"), Mkdir(path: "two")]
    b = [Mkdir(path: "two"), Mkdir(path: "one")]
    refute_equal Recipe.digest(a), Recipe.digest(b)
  end

  # A Within is not the same recipe as its contents at the top level:
  # the steps run somewhere else.
  def test_nesting_matters
    inner = [Run(argv: ["make"])]
    nested = [Within(dir: "build", steps: inner)]
    refute_equal Recipe.digest(nested), Recipe.digest(inner)
  end

  def test_a_hash_field_is_emitted_in_key_order
    a = Within(env: { "B" => "2", "A" => "1" }, steps: [])
    b = Within(env: { "A" => "1", "B" => "2" }, steps: [])
    assert_equal Recipe.canon(a), Recipe.canon(b)
  end

  #
  # It refuses what it does not know. A Pathname stringifies to an
  # absolute path, which would bake this machine's toolchain directory
  # into the identity of the build -- exactly the silent, plausible,
  # wrong answer an instrument must never give.
  #
  def test_canon_refuses_what_it_cannot_hash

    require 'pathname'

    for value in [Pathname.new("/tmp"), 1.5, Object.new, (1..2)] do
      err = assert_raises(Recipe::Error) { Recipe.canon([value]) }
      assert_match(/cannot hash/, err.message)
    end
  end
end

#
# Identity is the declared fields, and only those.
#
class TestRecipeIdentity < Minitest::Test

  include Recipe::DSL

  def test_a_defaulted_field_contributes_nothing
    spelled_out = Run(argv: ["make"], stdin: nil, status: 0)
    left_out    = Run(argv: ["make"])
    assert_equal Recipe.canon(left_out), Recipe.canon(spelled_out)
    assert_equal({ "argv" => ["make"] }, left_out.digest_map)
  end

  def test_a_field_away_from_its_default_is_identity
    refute_equal Recipe.canon(Run(argv: ["x"])),
                 Recipe.canon(Run(argv: ["x"], status: 3))
  end

  # The log says where output goes, not what gets built.
  def test_the_log_is_not_identity
    assert_equal Recipe.canon(Run(argv: ["make"], log: "build.log")),
                 Recipe.canon(Run(argv: ["make"], log: "make.log"))
  end

  def test_a_missing_required_field_is_refused
    err = assert_raises(Recipe::Error) { Recipe::Mkdir.new }
    assert_match(/path: is required/, err.message)
  end

  def test_an_unknown_field_is_refused
    err = assert_raises(Recipe::Error) { Run(argv: ["x"], drectory: "y") }
    assert_match(/no such field/, err.message)
  end

  def test_a_step_is_frozen
    assert_predicate Run(argv: ["make"]), :frozen?
  end

  def test_equality_is_between_identities
    assert_equal Run(argv: ["make"]), Run(argv: ["make"], log: "a.log")
    refute_equal Run(argv: ["make"]), Run(argv: ["make"], status: 1)
    refute_equal Run(argv: ["make"]), Mkdir(path: "make")
  end

  def test_every_kind_describes_itself_without_dying
    for k in Recipe::KINDS do
      step = case k.tag
             when "run"       then k.new(argv: ["x"])
             when "capture"   then k.new(bind: "b", argv: ["x"])
             when "mkdir"     then k.new(path: "d")
             when "copy", "move" then k.new(from: "a", to: "b")
             when "remove"    then k.new(paths: ["a"])
             when "symlink"   then k.new(target: "a", link: "b")
             when "chmod"     then k.new(path: "a", mode: 0755)
             when "write"     then k.new(path: "a", text: "t")
             when "read"      then k.new(bind: "b", path: "a")
             when "set"       then k.new(bind: "b", value: "v")
             when "extract"   then k.new(bind: "b", from: "$x", pattern: /a/)
             when "transform" then k.new(bind: "b", from: "$x",
                                         subs: [["a", "b"]])
             when "substitute" then k.new(path: "a", subs: [["a", "b"]])
             when "within"    then k.new(steps: [])
             when "prune"     then k.new
             end
      refute_nil step, "no sample for #{k.tag}"
      refute_empty step.describe
    end
  end
end

#
# THE EVOLUTION CONTRACT, as a test. These are the edits the model
# must absorb for years without rebuilding the world.
#
class TestRecipeEvolution < Minitest::Test

  include Recipe::DSL

  # Two tags that exist only here, so that reopening and extending
  # them cannot affect anything real.
  class Widget < Recipe::Step
    def self.tag = "test-widget"
    field :a
  end

  # The same tag and fields under a different class name: identity is
  # the TAG, which is why a Ruby class may be renamed for free.
  class WidgetRenamed < Recipe::Step
    def self.tag = "test-widget"
    field :a
  end

  def test_adding_an_optional_field_changes_no_existing_digest

    before = Recipe.digest([Widget.new(a: "x")])
    Widget.field(:timeout, nil)             # the edit under test
    after = Recipe.digest([Widget.new(a: "x")])

    assert_equal before, after

    # ...and it is identity the moment a recipe uses it.
    refute_equal before, Recipe.digest([Widget.new(a: "x", timeout: 30)])
  end

  def test_renaming_the_ruby_class_changes_no_digest
    assert_equal Recipe.canon([Widget.new(a: "x")]),
                 Recipe.canon([WidgetRenamed.new(a: "x")])
  end

  def test_adding_a_kind_changes_no_existing_digest

    before = Recipe.digest([Run(argv: ["make"])])

    Class.new(Recipe::Step) do
      def self.tag = "test-brand-new-kind"
      field :whatever
    end

    assert_equal before, Recipe.digest([Run(argv: ["make"])])
  end

  #
  # The tags of the shipped kinds. A tag is the name a recipe is
  # recorded under, so renaming one rebuilds everything: this list
  # makes that an edit to a test rather than an accident.
  #
  TAGS = %w[capture chmod copy extract mkdir move prune read remove run
            set substitute symlink transform within write].freeze

  def test_the_shipped_tags_are_these
    assert_equal TAGS, Recipe::KINDS.map(&:tag).sort
  end

  def test_tags_are_unique
    tags = Recipe::KINDS.map(&:tag)
    assert_equal tags.length, tags.uniq.length
  end

  def test_every_kind_has_a_constructor
    for k in Recipe::KINDS do
      assert_includes Recipe::DSL.instance_methods,
                      k.name.split("::").last.to_sym
    end
  end
end

#
# Token expansion: what a step's strings mean when they run.
#
class TestRecipeTokens < Minitest::Test

  def ctx(tokens = { "PREFIX" => "/opt/x", "PAR" => 4 })
    return Recipe::Ctx.new(root: Dir.tmpdir, tokens: tokens)
  end

  def test_a_token_expands
    assert_equal "/opt/x/bin", ctx.expand("$PREFIX/bin")
    assert_equal "-j4", ctx.expand("-j$PAR")
  end

  # pkg-config's own syntax must survive a recipe untouched, which is
  # why the braced form is not a token at all.
  def test_braces_are_not_tokens
    assert_equal 'prefix=${pcfiledir}/../..',
                 ctx.expand('prefix=${pcfiledir}/../..')
  end

  # A shell snippet keeps its own variables by doubling the dollar.
  def test_a_doubled_dollar_is_a_literal_one
    assert_equal 'echo $FOO "$@"', ctx.expand('echo $$FOO "$@"')
  end

  def test_an_unknown_token_is_refused
    err = assert_raises(Recipe::Error) { ctx.expand("$NOPE/bin") }
    assert_match(/unknown token \$NOPE/, err.message)
  end

  def test_a_step_may_not_shadow_a_builtin
    err = assert_raises(Recipe::Error) { ctx.bind("PREFIX", "/elsewhere") }
    assert_match(/builtin token/, err.message)
  end

  def test_a_bound_value_expands_like_any_other
    c = ctx
    c.bind("specs_dir", "/tmp/somewhere")
    assert_equal "/tmp/somewhere/specs", c.expand("$specs_dir/specs")
  end
end

#
# What the steps actually DO. A model that hashes beautifully and
# builds the wrong thing is worse than no model at all.
#
class TestRecipeExecution < Minitest::Test

  include Recipe::DSL

  def in_tree(tokens: {})
    Dir.mktmpdir("pkgmgr-recipe-") do |root|
      yield root, Recipe::Ctx.new(root: root, tokens: tokens)
    end
  end

  def run_steps(steps, ctx) = Recipe.run(steps, ctx)

  def test_the_filesystem_kinds
    in_tree do |root, c|
      run_steps([
        Mkdir(path: "a/b"),
        Write(path: "a/b/one.txt", text: "hello"),
        Copy(from: "a/b/one.txt", to: "a/b/two.txt"),
        Move(from: "a/b/two.txt", to: "a/three.txt"),
        Chmod(path: "a/three.txt", mode: 0640),
        Symlink(target: "three.txt", link: "a/link"),
      ], c)

      assert_equal "hello", File.read("#{root}/a/b/one.txt")
      assert_equal "hello", File.read("#{root}/a/three.txt")
      refute File.exist?("#{root}/a/b/two.txt")
      assert_equal 0640, File.stat("#{root}/a/three.txt").mode & 0777
      assert_equal "three.txt", File.readlink("#{root}/a/link")
    end
  end

  def test_remove_is_idempotent_and_globs
    in_tree do |root, c|
      run_steps([
        Mkdir(path: "d"),
        Write(path: "d/a.la", text: ""),
        Write(path: "d/b.la", text: ""),
        Write(path: "d/keep.so", text: ""),
        Remove(paths: ["d/*.la", "not-there-at-all"]),
      ], c)

      assert_empty Dir.glob("#{root}/d/*.la")
      assert File.exist?("#{root}/d/keep.so")
    end
  end

  # Copying nothing is a typo, not a success.
  def test_copy_refuses_to_match_nothing
    in_tree do |root, c|
      err = assert_raises(Recipe::Error) {
        run_steps([Copy(from: "nope/*", to: "dst")], c)
      }
      assert_match(/nothing matches/, err.message)
    end
  end

  def test_a_glob_of_several_needs_a_directory
    in_tree do |root, c|
      err = assert_raises(Recipe::Error) {
        run_steps([
          Mkdir(path: "s"),
          Write(path: "s/a", text: ""),
          Write(path: "s/b", text: ""),
          Copy(from: "s/*", to: "dst-file"),
        ], c)
      }
      assert_match(/must be an existing directory/, err.message)
    end
  end

  def test_run_and_its_status
    in_tree do |root, c|
      run_steps([Run(argv: ["sh", "-c", "echo out > made.txt"])], c)
      assert_equal "out\n", File.read("#{root}/made.txt")

      run_steps([Run(argv: ["sh", "-c", "exit 3"], status: 3)], c)

      err = assert_raises(Recipe::Error) {
        run_steps([Run(argv: ["sh", "-c", "exit 1"])], c)
      }
      assert_match(/exit status 1, expected 0/, err.message)
    end
  end

  def test_a_log_collects_what_a_command_printed
    in_tree do |root, c|
      run_steps([
        Run(argv: ["sh", "-c", "echo one; echo two >&2"], log: "b.log"),
      ], c)
      assert_match(/one/, File.read("#{root}/b.log"))
      assert_match(/two/, File.read("#{root}/b.log"))
    end
  end

  def test_within_scopes_the_directory_and_the_environment
    in_tree do |root, c|
      run_steps([
        Mkdir(path: "build"),
        Within(dir: "build", env: { "GREETING" => "hi" }, steps: [
          Run(argv: ["sh", "-c", "echo $$GREETING > g.txt"]),
        ]),
      ], c)

      assert_equal "hi\n", File.read("#{root}/build/g.txt")
      refute File.exist?("#{root}/g.txt")
    end
  end

  def test_within_refuses_a_directory_that_is_not_there
    in_tree do |root, c|
      err = assert_raises(Recipe::Error) {
        run_steps([Within(dir: "nope", steps: [])], c)
      }
      assert_match(/no such directory/, err.message)
    end
  end

  #
  # The host_gcc shape: learn something from what was just built,
  # derive a file from it, write it out.
  #
  def test_capture_extract_transform_write
    in_tree do |root, c|
      run_steps([
        Capture(bind: "dirs",
                argv: ["sh", "-c", "echo 'install: /somewhere/lib/gcc'"]),
        Extract(bind: "dir", from: "$dirs", pattern: /^install:\s*(.+)$/),
        Set(bind: "specs", value: "*link:\n%{!static:-dynamic-linker /lib/ld}"),
        Transform(bind: "specs", from: "$specs",
                  subs: [["/lib/ld", "$STACK_LOADER"]]),
        Write(path: "out.specs", text: "$dir\n$specs\n"),
      ], Recipe::Ctx.new(root: root,
                         tokens: { "STACK_LOADER" => "/tc/lib/ld-ours" }))

      text = File.read("#{root}/out.specs")
      assert_match(%r{^/somewhere/lib/gcc$}, text)
      assert_match(%r{-dynamic-linker /tc/lib/ld-ours}, text)
      refute_match(%r{-dynamic-linker /lib/ld$}, text)
    end
  end

  #
  # A rewrite that matches nothing is the bug, not a no-op: the
  # hardcoded interpreter moved and the substitution silently did
  # nothing, which is how a non-portable compiler shipped.
  #
  def test_a_substitution_that_matches_nothing_fails
    in_tree do |root, c|
      c.bind("v", "hello world")

      err = assert_raises(Recipe::Error) {
        run_steps([Transform(bind: "v", from: "$v",
                             subs: [["goodbye", "hi"]])], c)
      }
      assert_match(/matches nothing/, err.message)
    end
  end

  def test_extract_that_matches_nothing_fails
    in_tree do |root, c|
      c.bind("v", "nothing useful here")
      err = assert_raises(Recipe::Error) {
        run_steps([Extract(bind: "x", from: "$v", pattern: /^install: (.+)$/)],
                  c)
      }
      assert_match(/matches nothing/, err.message)
    end
  end

  #
  # The ncurses shape: rewrite the staged prefix baked into every .pc
  # file the build just installed.
  #
  def test_substitute_across_a_glob
    in_tree do |root, c|
      run_steps([
        Mkdir(path: "pc"),
        Write(path: "pc/a.pc", text: "prefix=/staged\nLibs: -L/staged/lib\n"),
        Write(path: "pc/b.pc", text: "prefix=/staged\nLibs: -L/staged/lib\n"),
        Substitute(path: "pc/*.pc", subs: [
          [/^prefix=.+$/, 'prefix=${pcfiledir}/../..'],
          ["/staged", '${prefix}'],
        ]),
      ], c)

      for f in %w[a b] do
        text = File.read("#{root}/pc/#{f}.pc")
        assert_equal "prefix=${pcfiledir}/../..\nLibs: -L${prefix}/lib\n", text
      end
    end
  end

  def test_substitute_fails_when_no_file_matches_a_sub
    in_tree do |root, c|
      err = assert_raises(Recipe::Error) {
        run_steps([
          Mkdir(path: "pc"),
          Write(path: "pc/a.pc", text: "prefix=/staged\n"),
          Substitute(path: "pc/*.pc", subs: [["nowhere", "x"]]),
        ], c)
      }
      assert_match(/matches nothing in any/, err.message)
    end
  end

  def test_read_binds_a_file
    in_tree do |root, c|
      run_steps([
        Write(path: "v.txt", text: "3.4.7"),
        Read(bind: "ver", path: "v.txt"),
        Write(path: "copy.txt", text: "version $ver"),
      ], c)
      assert_equal "version 3.4.7", File.read("#{root}/copy.txt")
    end
  end

  #
  # Prune keeps the install and the logs, and lifts the logs of an
  # out-of-tree build out of the directory that is about to go.
  #
  def test_prune
    in_tree do |root, c|
      run_steps([
        Mkdir(path: "install/bin"),
        Write(path: "install/bin/tool", text: "x"),
        Mkdir(path: "build"),
        Write(path: "build/configure.log", text: "log"),
        Mkdir(path: "src"),
        Write(path: "top.log", text: "top"),
        Prune(),
      ], c)

      assert File.exist?("#{root}/install/bin/tool")
      assert File.exist?("#{root}/top.log")
      assert_equal "log", File.read("#{root}/build-configure.log")
      refute File.exist?("#{root}/build")
      refute File.exist?("#{root}/src")
    end
  end

  # A value bound inside a scope is still there after it: the binds
  # belong to the run, not to the directory it happened to be in.
  def test_binds_outlive_the_scope_that_made_them
    in_tree do |root, c|
      run_steps([
        Mkdir(path: "build"),
        Within(dir: "build", steps: [
          Capture(bind: "who", argv: ["sh", "-c", "echo inner"]),
        ]),
        Write(path: "who.txt", text: "$who"),
      ], c)
      assert_equal "inner\n", File.read("#{root}/who.txt")
    end
  end
end
