# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'
require_relative '../source_digest'
require_relative '../build_inputs'

#
# Fingerprinting the code that builds a package.
#
# The properties that make this usable rather than merely correct: it
# must notice a changed flag, and must NOT notice a reworded comment,
# because a 30-minute rebuild for a prose edit is the kind of price
# that makes people switch a safety mechanism off.
#
class TestSourceDigest < Minitest::Test

  include TestHelper

  def write_rb(dir, body)
    path = File.join(dir, "probe.rb")
    File.write(path, body)
    return path
  end

  SAMPLE = <<~RB
    class Probe
      # A comment that says something.
      def build
        run("make", "V=1")     # trailing comment
      end

      def other
        42
      end
    end
  RB

  def test_extracts_one_method_not_the_file
    Dir.mktmpdir do |d|
      f = write_rb(d, SAMPLE)
      src = SourceDigest.method_source(f, :build)
      assert_includes src, "make"
      refute_includes src, "42"
    end
  end

  def test_comments_do_not_change_the_digest
    Dir.mktmpdir do |d|
      a = SourceDigest.method_source(write_rb(d, SAMPLE), :build)
      SourceDigest.instance_variable_set(:@cache, nil)
      reworded = SAMPLE.sub("# A comment that says something.",
                            "# TOTALLY DIFFERENT PROSE")
                       .sub("# trailing comment", "# also different")
      b = SourceDigest.method_source(write_rb(d, reworded), :build)
      assert_equal a, b
    end
  end

  def test_a_changed_flag_changes_the_digest
    Dir.mktmpdir do |d|
      a = SourceDigest.method_source(write_rb(d, SAMPLE), :build)
      SourceDigest.instance_variable_set(:@cache, nil)
      changed = SAMPLE.sub('"V=1"', '"V=0"')
      b = SourceDigest.method_source(write_rb(d, changed), :build)
      refute_equal a, b
    end
  end

  # Moving a method within its file is not a change to what it does.
  def test_order_does_not_matter
    Dir.mktmpdir do |d|
      f1 = write_rb(d, SAMPLE)
      d1 = SourceDigest.digest(SourceDigest.method_source(f1, :build),
                               SourceDigest.method_source(f1, :other))
      SourceDigest.instance_variable_set(:@cache, nil)

      swapped = <<~RB
        class Probe
          def other
            42
          end

          def build
            run("make", "V=1")
          end
        end
      RB
      f2 = write_rb(d, swapped)
      d2 = SourceDigest.digest(SourceDigest.method_source(f2, :build),
                               SourceDigest.method_source(f2, :other))
      assert_equal d1, d2
    end
  end

  def test_missing_file_is_not_a_crash
    assert_equal({}, SourceDigest.parse_defs("/nonexistent/x.rb"))
  end
end


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

        inst = pkgmgr.get("foo").find_install(Ver("1.0.0"))
        refute_nil inst
        assert File.file?(inst.path / BuildInputs::FILE)
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

        refute pkg.build_inputs_changed?(Ver("1.0.0"))
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

        assert pkg.build_inputs_changed?(Ver("1.0.0"))
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
        assert pkg.build_inputs_changed?(Ver("1.0.0"))
      end
    end
  end

  # An install with no record predates the mechanism or was made by
  # hand. Calling it stale would rebuild the world on no evidence.
  def test_an_install_without_a_record_is_not_stale
    with_fake_tc do
      with_stubbed_externals do
        reset_pkgmgr!
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")
        pkgmgr.refresh

        FileUtils.rm_f(pkg.find_install(Ver("1.0.0")).path /
                       BuildInputs::FILE)
        refute pkg.build_inputs_changed?(Ver("1.0.0"))
      end
    end
  end

  # A flag that is not declared cannot be passed: the helpers ask the
  # package for its flags rather than accepting them, so there is no
  # way to build with an argument that goes unrecorded.
  def test_the_build_helpers_take_no_flag_argument
    [:meson_stack_build, :autotools_stack_build].each do |m|
      params = Package.instance_method(m).parameters
      assert_equal [[:req, :install_dir]], params,
                   "#{m} must not accept flags: they would go unrecorded"
    end
  end

  def test_base_class_declares_no_flags
    with_fake_tc do
      assert_empty FakePackage.new("foo").build_flags
    end
  end
end
