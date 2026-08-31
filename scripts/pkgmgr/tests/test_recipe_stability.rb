# SPDX-License-Identifier: BSD-2-Clause
#
# A recipe digest has to be reproducible.
#
# It is the whole basis for "this install still matches its sources",
# so an answer that depends on anything but the sources makes the
# check worse than useless: it reports a rebuild that changes nothing,
# and the next person learns to ignore it.
#
# The one that got through: glycin's meson flags named its cross file
# with File.expand_path, which resolves against the current working
# directory. A staleness check runs wherever the caller stands, so
# CMake -- configuring from a build directory -- reported the glycin
# packages as stale while the identical check run from the repository
# root said they were fine.
#

require_relative 'test_helper'
require_relative '../glycin'
require_relative '../tcc'
require_relative '../package'

class TestRecipeDigestIsReproducible < Minitest::Test

  include TestHelper

  # Instantiated directly rather than fetched from the registry: this
  # asks about the class, and must not care what a neighbouring test
  # did to the package list.
  # tcc is here for the opposite reason to glycin's: its recipe NAMES a
  # value that lives inside the extracted source ($SRC_REF), which is
  # exactly what must not be read while computing a digest.
  PACKAGES = [HostLibglycinPackage, HostGlycinLoadersPackage,
              TccPackage].freeze

  def digest_from(dir, pkg)
    return Dir.chdir(dir) { pkg.build_recipe_digest(pkg.default_ver) }
  end

  def test_the_digest_does_not_depend_on_the_working_directory
    Dir.mktmpdir("pkgmgr-cwd-") do |elsewhere|
      for klass in PACKAGES do
        pkg = klass.new
        assert_equal digest_from(MAIN_DIR.to_s, pkg),
                     digest_from(elsewhere, pkg),
                     "#{pkg.name}: the recipe digest moved with the cwd"
      end
    end
  end

  # The same statement, made where it is easier to read when it fails:
  # a flag that contains the directory the command was run from is a
  # flag that will not hash the same twice.
  # The token stays a token in the recipe. If it ever resolved while
  # the digest was being computed, the digest would depend on whether
  # a source tree happened to be there -- and on which one.
  def test_src_ref_is_not_resolved_into_the_recipe
    pkg = TccPackage.new
    steps = pkg.build_steps.map { |s| s.argv.join(" ") }.join(" ")

    assert_includes steps, "$SRC_REF",
                    "tcc no longer names the source ref"
    refute_includes steps, "98765e5e",
                    "the ref was baked into the recipe"
  end

  def test_no_flag_names_the_working_directory
    Dir.mktmpdir("pkgmgr-cwd-") do |elsewhere|
      for klass in PACKAGES do
        pkg = klass.new
        Dir.chdir(elsewhere) do
          flags = pkg.build_flags(pkg.default_ver).join(" ")
          refute_includes flags, elsewhere,
                          "#{pkg.name} names its own working directory"
        end
      end
    end
  end
end
