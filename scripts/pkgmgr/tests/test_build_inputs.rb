# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'
require_relative '../build_inputs'
require_relative '../micropython'

#
# The record itself.
#
class TestBuildInputsRecord < Minitest::Test

  include TestHelper

  # Absolute paths would make the file differ between machines for
  # reasons that have nothing to do with the build.
  def test_paths_are_normalised
    with_fake_tc do |tc|
      text = BuildInputs.normalize("#{tc}/staging/foo -j16 #{MAIN_DIR}/x")
      assert_includes text, "$TC/staging/foo"
      assert_includes text, "$SRC/x"
      assert_includes text, "-j$PAR"
      refute_includes text, tc.to_s
    end
  end

  def test_render_lists_files_with_digests
    Dir.mktmpdir do |d|
      f = Pathname.new(File.join(d, "a.diff"))
      File.write(f, "hello")
      out = BuildInputs.render(recipe: "sha256:abc", files: [f])
      assert_match(/^recipe sha256:abc$/, out)
      assert_match(/^file   .*a\.diff sha256:[0-9a-f]{32}$/, out)
    end
  end

  # argv is recorded for a human to read, but never compared: it is
  # not knowable before the build runs, so comparing it would make
  # every install look stale.
  def test_argv_is_recorded_but_not_compared
    Dir.mktmpdir do |d|
      dir = Pathname.new(d)
      BuildInputs.write(dir, recipe: "sha256:abc", files: [],
                        argv: "meson setup build --prefix=/x")
      text = File.read(dir / BuildInputs::FILE)
      assert_includes text, "argv"
      refute_includes BuildInputs.comparable(dir), "argv"
    end
  end

  def test_comparable_is_nil_without_a_record
    Dir.mktmpdir { |d| assert_nil BuildInputs.comparable(Pathname.new(d)) }
  end
end


#
# End to end, through a real install.
#
class TestBuildIdentity < Minitest::Test

  include TestHelper

  def test_install_records_what_it_was_built_from
    with_fake_tc do
      with_stubbed_externals do
        reset_pkgmgr!
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")

        inst = bound(pkgmgr.get("foo")).find_install(Ver("1.0.0"))
        refute_nil inst
        assert File.file?(inst.path / BuildInputs::FILE)
      end
    end
  end

  #
  # A recipe that reads differently per architecture.
  #
  # zlib is the real one: its build step names the archiver as
  # "#{default_arch.gcc_tc}-linux-ar", so the same version installed
  # for two arches was genuinely built from two different recipes.
  # A digest is only meaningful together with the arch it was computed
  # for, and both writing and checking have to say which they mean.
  #
  class ArchVaryingPackage < TestHelper::FakePackage
    def build_flags(ver = nil) = ["--ar=#{default_arch.gcc_tc}-linux-ar"]
  end

  def two_arch_pkg
    return ArchVaryingPackage.new(
      "archy", arch_list: [ALL_ARCHS["i386"], ALL_ARCHS["riscv64"]]
    )
  end

  def test_each_arch_records_its_own_recipe
    with_fake_tc do
      with_stubbed_externals do
        reset_pkgmgr!
        pkg = two_arch_pkg
        pkgmgr.register(pkg)

        for a in ["i386", "riscv64"] do
          with_context(ARCH: ALL_ARCHS[a], BOARD: nil) do
            pkgmgr.install("archy")
          end
        end
        pkgmgr.refresh

        digests = pkg.get_install_list.map { |i|
          BuildInputs.comparable(i.path)
        }
        assert_equal 2, digests.length
        refute_equal digests[0], digests[1],
                     "two arches, two recipes, two digests"
      end
    end
  end

  # ...and each of them reads back as OK, from either arch. Before,
  # every install was checked against the CURRENT arch's recipe, so
  # the whole set went stale as soon as the other arch was selected.
  def test_neither_arch_is_stale_from_the_other
    with_fake_tc do
      with_stubbed_externals do
        reset_pkgmgr!
        pkg = two_arch_pkg
        pkgmgr.register(pkg)

        for a in ["i386", "riscv64"] do
          with_context(ARCH: ALL_ARCHS[a], BOARD: nil) do
            pkgmgr.install("archy")
          end
        end
        pkgmgr.refresh

        for a in ["i386", "riscv64"] do
          with_context(ARCH: ALL_ARCHS[a], BOARD: nil) do
            assert_empty pkgmgr.get_stale_packages.map(&:name),
                         "stale when viewed from #{a}"
          end
        end
      end
    end
  end

  # Installing for one arch must not touch the other arch's record.
  # Recording "every install of this version" stamped trees the call
  # never rebuilt, certifying an old binary against today's recipe.
  def test_installing_one_arch_leaves_the_other_record_alone
    with_fake_tc do
      with_stubbed_externals do
        reset_pkgmgr!
        pkg = two_arch_pkg
        pkgmgr.register(pkg)

        with_context(ARCH: ALL_ARCHS["riscv64"], BOARD: nil) do
          pkgmgr.install("archy")
        end
        pkgmgr.refresh

        rv = pkg.get_install_list.first
        before = BuildInputs.comparable(rv.path)

        with_context(ARCH: ALL_ARCHS["i386"], BOARD: nil) do
          pkgmgr.install("archy")
        end
        pkgmgr.refresh

        assert_equal before, BuildInputs.comparable(rv.path),
                     "the riscv64 record was rewritten by an i386 install"
      end
    end
  end

  def test_an_unchanged_package_is_not_stale
    with_fake_tc do
      with_stubbed_externals do
        reset_pkgmgr!
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")
        pkgmgr.refresh

        b = bound(pkg)
        refute b.build_inputs_changed?(b.find_install(Ver("1.0.0")))
        assert_empty pkgmgr.get_stale_packages
      end
    end
  end

  # The case that motivated all of this: a patch appears, and the
  # installed artifact no longer matches the sources.
  def test_a_new_patch_makes_it_stale
    with_fake_tc do
      with_stubbed_externals do
        reset_pkgmgr!
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")
        pkgmgr.refresh

        # A patch the install never saw.
        pkg.define_singleton_method(:build_files) { |ver = nil|
          [Pathname.new(__FILE__)]
        }

        b = bound(pkg)
        assert b.build_inputs_changed?(b.find_install(Ver("1.0.0")))
        assert_includes pkgmgr.get_stale_packages.map(&:name), "foo"
      end
    end
  end

  def test_a_changed_flag_makes_it_stale
    with_fake_tc do
      with_stubbed_externals do
        reset_pkgmgr!
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")
        pkgmgr.refresh

        pkg.define_singleton_method(:build_flags) { |ver = nil|
          ["--newly-added"]
        }
        b = bound(pkg)
        assert b.build_inputs_changed?(b.find_install(Ver("1.0.0")))
      end
    end
  end

  # A missing record is "I do not know", and toolchain5 starts empty
  # so every install is made by this mechanism: a record that is not
  # there means something went wrong. Two real cases were invisible
  # while this counted as fine -- expat's record write raised midway
  # through a rebuild, and gnuefi recorded one arch out of three.
  def test_an_install_without_a_record_is_unknown_and_needs_rebuilding
    with_fake_tc do
      with_stubbed_externals do
        reset_pkgmgr!
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")
        pkgmgr.refresh

        FileUtils.rm_f(bound(pkg).find_install(Ver("1.0.0")).path /
                       BuildInputs::FILE)

        b = bound(pkg)
        i = b.find_install(Ver("1.0.0"))
        assert_equal :unknown, b.build_inputs_state_of(i)
        assert b.build_inputs_changed?(i)
      end
    end
  end

  def test_the_three_states_are_distinguished
    with_fake_tc do
      with_stubbed_externals do
        reset_pkgmgr!
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)

        b = bound(pkg)
        assert_equal :not_installed,
                     b.build_inputs_state_of(b.find_install(Ver("1.0.0")))

        pkgmgr.install("foo")
        pkgmgr.refresh
        b = bound(pkg)
        assert_equal :ok, b.build_inputs_state_of(b.find_install(Ver("1.0.0")))

        pkg.define_singleton_method(:build_flags) { |v = nil| ["--x"] }
        b = bound(pkg)
        assert_equal :changed,
                     b.build_inputs_state_of(b.find_install(Ver("1.0.0")))
      end
    end
  end

  # An argument that goes unrecorded is impossible by construction
  # now: build_steps is both what runs and what is hashed, so a flag
  # reaches a compiler only by being in an argv, and every argv is in
  # the digest.
  def test_nothing_reaches_a_build_without_reaching_the_digest
    with_fake_tc do
      plain = FakePackage.new("foo")
      extra = FakePackage.new("foo")

      plain.define_singleton_method(:build_steps) { |v = nil|
        [Recipe::Run.new(log: "b.log", argv: ["make"])]
      }
      extra.define_singleton_method(:build_steps) { |v = nil|
        [Recipe::Run.new(log: "b.log", argv: ["make", "--undeclared"])]
      }

      refute_equal plain.build_recipe_digest, extra.build_recipe_digest
    end
  end

  def test_base_class_declares_no_flags
    with_fake_tc do
      assert_empty FakePackage.new("foo").build_flags
    end
  end
end


#
# The declarative step runner (see recipe.rb).
#
# A Run is a command and a Within is the scope around it -- directory,
# environment, removals -- which is the smallest vocabulary that
# covers what the tree actually does: micropython builds two
# components in two directories, one of which must NOT inherit the
# cross compiler.
#
class TestBuildSteps < Minitest::Test

  include TestHelper
  include Recipe::DSL

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  class StepPkg < TestHelper::FakePackage
    attr_accessor :steps, :ran
    def build_steps(ver = nil) = (@steps || [])
  end

  # Registered, because a build context asks the registry for the
  # package's dependency closure: the tokens its recipe may name.
  def pkg_with(steps, name: "stepped")
    p = StepPkg.new(name)
    pkgmgr.register(p)
    p.steps = steps
    p.ran = []
    # Capture instead of executing: what matters here is WHICH
    # command would run, in which directory, with which environment.
    p.define_singleton_method(:run_command) do |log, argv, env: nil|
      @ran << [log, argv, Dir.pwd, env]
      true
    end
    return p
  end

  def test_tokens_are_expanded
    with_fake_tc do
      p = pkg_with([Run(log: "b.log",
                        argv: ["make", "-j$PAR", "P=$INSTALL/x"])])
      Dir.mktmpdir { |d| p.run_build_steps(Pathname.new(d)) }
      _, argv, = p.ran.first
      assert_equal "make", argv[0]
      assert_equal "-j#{BUILD_PAR}", argv[1]
      assert_match(%r{\AP=/.*/x\z}, argv[2])
      refute_includes argv[2], "$INSTALL"
    end
  end

  def test_steps_run_in_order
    with_fake_tc do
      p = pkg_with([Run(log: "a.log", argv: ["one"]),
                    Run(log: "b.log", argv: ["two"])])
      Dir.mktmpdir { |d| p.run_build_steps(Pathname.new(d)) }
      assert_equal ["a.log", "b.log"], p.ran.map(&:first)
    end
  end

  # A failing step stops the build: the ones after it would compile
  # against whatever the failed one did not produce.
  def test_a_failing_step_stops_the_rest
    with_fake_tc do
      p = pkg_with([Run(log: "a.log", argv: ["one"]),
                    Run(log: "b.log", argv: ["two"])])
      p.define_singleton_method(:run_command) { |log, argv, env: nil|
        @ran << [log, argv, Dir.pwd, env]
        false
      }
      Dir.mktmpdir { |d| refute p.run_build_steps(Pathname.new(d)) }
      assert_equal ["a.log"], p.ran.map(&:first)
    end
  end

  def test_dir_changes_the_working_directory
    with_fake_tc do
      Dir.mktmpdir do |d|
        FileUtils.mkdir_p(File.join(d, "sub"))
        p = pkg_with([Within(dir: "sub", steps: [
          Run(log: "b.log", argv: ["x"]),
        ])])
        FileUtils.chdir(d) { p.run_build_steps(Pathname.new(d)) }
        assert_equal "sub", File.basename(p.ran.first[2])
      end
    end
  end

  # The environment goes TO the command rather than into this
  # process: it is then logged with the command it belongs to, and
  # there is nothing to restore afterwards.
  def test_env_goes_to_the_command_not_into_the_process
    with_fake_tc do
      p = pkg_with([Within(env: { "PROBE" => "set" }, steps: [
        Run(log: "b.log", argv: ["x"]),
      ])])
      Dir.mktmpdir { |d| p.run_build_steps(Pathname.new(d)) }
      assert_equal({ "PROBE" => "set" }, p.ran.first[3])
      assert_nil ENV["PROBE"]
    end
  end

  # micropython's mpy-cross must not see the cross compiler. A nil
  # value is how spawn is told to REMOVE a variable, which is not the
  # same as setting it to the empty string.
  def test_unset_removes_a_variable_for_the_step_only
    with_fake_tc do
      ENV["GONE"] = "yes"
      p = pkg_with([Within(unset: ["GONE"], steps: [
        Run(log: "b.log", argv: ["x"]),
      ])])
      Dir.mktmpdir { |d| p.run_build_steps(Pathname.new(d)) }
      assert_equal({ "GONE" => nil }, p.ran.first[3])
      assert_equal "yes", ENV["GONE"], "and untouched in this process"
    ensure
      ENV.delete("GONE")
    end
  end

  # The steps are the recipe, so changing one is a rebuild.
  def test_steps_are_part_of_the_fingerprint
    with_fake_tc do
      a = pkg_with([Run(log: "b.log", argv: ["make", "X=1"])], name: "a")
      b = pkg_with([Run(log: "b.log", argv: ["make", "X=2"])], name: "b")
      refute_equal a.build_recipe_digest, b.build_recipe_digest
    end
  end

  # micropython is the case the mechanism was shaped around.
  def test_micropython_declares_its_two_components
    steps = MicropythonPackage.new.build_steps
    dirs = steps.map(&:dir)
    assert_includes dirs, "mpy-cross"
    assert_includes dirs, "ports/unix"

    mpy = steps.find { |s| s.dir == "mpy-cross" }
    assert_includes mpy.unset, "CC"
    assert_includes mpy.unset, "CROSS_COMPILE"

    unix = steps.select { |s| s.dir == "ports/unix" }
    assert_equal 2, unix.length, "submodules, then the port itself"
    assert_equal "-static", unix.last.env["LDFLAGS_EXTRA"]
  end
end


#
# Two more lines of the fingerprint's ledger.
#
# clean_build is recovery, not recipe: it runs before a resumed build
# and only deletes, so what gets built is what a fresh extraction
# would have built. Removing thirty-six copies of it must not flag a
# single install. host_compiler_gnu17 is the opposite case: a shared
# helper whose text reaches configure's argv, called by name, so its
# body has to be fingerprinted for the packages that call it.
#
class TestWhatTheFingerprintCounts < Minitest::Test

  include TestHelper

  class WithCleanBuild < TestHelper::FakePackage
    def build_flags(ver = nil) = ["--same"]
    def clean_build(dir) = true
  end

  class WithoutCleanBuild < TestHelper::FakePackage
    def build_flags(ver = nil) = ["--same"]
  end

  def test_a_clean_build_override_is_not_part_of_the_recipe
    with_fake_tc do
      assert_equal WithoutCleanBuild.new("a").build_recipe_digest,
                   WithCleanBuild.new("b").build_recipe_digest
    end
  end
end


#
# WHICH SCHEME WROTE THE RECORD.
#
# A digest computed one way cannot disagree with one computed the
# other; it can only be incomparable. Saying "built from other
# sources" about that would be the instrument blaming the sources for
# something the fingerprint did -- and on a toolchain built before the
# recipes became data, that is most of the tree.
#
class TestRecordFormat < Minitest::Test

  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def installed_one
    pkg = FakePackage.new("foo")
    pkgmgr.register(pkg)
    pkgmgr.install("foo")
    pkgmgr.refresh
    return [pkg, bound(pkg).find_install(Ver("1.0.0"))]
  end

  def rewrite(inst, recipe: nil, drop_format: false)
    f = inst.path / BuildInputs::FILE
    lines = File.read(f).lines
    lines = lines.reject { |l| l.start_with?("format ") } if drop_format
    lines = lines.map { |l|
      l.start_with?("recipe ") && recipe ? "recipe #{recipe}\n" : l
    }
    File.write(f, lines.join)
  end

  # The reader on its own: the line when there is one, 1 when the
  # record predates the line, and nothing when there is no record.
  def test_format_of_reads_the_line_and_says_nothing_of_no_record
    Dir.mktmpdir do |d|
      dir = Pathname(d)
      assert_nil BuildInputs.format_of(dir)
      File.write(dir / BuildInputs::FILE, "recipe sha256:0\n")
      assert_equal 1, BuildInputs.format_of(dir)
      File.write(dir / BuildInputs::FILE, "recipe sha256:0\nformat 7\n")
      assert_equal 7, BuildInputs.format_of(dir)
    end
  end

  def test_the_record_says_which_scheme_wrote_it
    with_fake_tc do
      with_stubbed_externals do
        _, inst = installed_one
        assert_match(/^format #{BuildInputs::FORMAT}$/,
                     File.read(inst.path / BuildInputs::FILE))
      end
    end
  end

  #
  # The promise this file has always made, now true on both sides:
  # adding an informational line cannot make an install look stale.
  # Only the recorded side was filtered, so `format` -- the first
  # informational line that is always written -- made eight healthy
  # installs read changed the moment it existed.
  #
  def test_an_informational_line_is_not_part_of_the_comparison
    with_fake_tc do
      with_stubbed_externals do
        pkg, inst = installed_one
        rewrite(inst, drop_format: true)
        assert_equal :ok, bound(pkg).build_inputs_state_of(inst)
      end
    end
  end

  def test_a_record_from_an_older_scheme_says_so
    with_fake_tc do
      with_stubbed_externals do
        pkg, inst = installed_one
        rewrite(inst, recipe: "sha256:00000000000000000000000000000000",
                drop_format: true)
        assert_equal :old_format, bound(pkg).build_inputs_state_of(inst)
      end
    end
  end

  # The same mismatch, recorded by THIS scheme, is a real change.
  def test_a_mismatch_in_the_current_format_is_a_change
    with_fake_tc do
      with_stubbed_externals do
        pkg, inst = installed_one
        rewrite(inst, recipe: "sha256:00000000000000000000000000000000")
        assert_equal :changed, bound(pkg).build_inputs_state_of(inst)
      end
    end
  end

  # Both still mean "do not trust this install".
  def test_either_way_it_needs_attention
    with_fake_tc do
      with_stubbed_externals do
        pkg, inst = installed_one
        rewrite(inst, recipe: "sha256:00000000000000000000000000000000",
                drop_format: true)
        assert bound(pkg).build_inputs_changed?(inst)
      end
    end
  end

  # ...and the listing explains which of the two it is.
  def test_the_listing_explains_an_older_format
    with_fake_tc do
      with_stubbed_externals do
        pkg, inst = installed_one
        rewrite(inst, recipe: "sha256:00000000000000000000000000000000",
                drop_format: true)

        out = capture_io { pkgmgr.show_status_all }.first
                .gsub(/\e\[[0-9;]*m/, "")

        assert_match(/older recipe format/, out)
        assert_match(/not because\ntheir sources changed/, out)
      end
    end
  end
end
