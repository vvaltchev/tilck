# SPDX-License-Identifier: BSD-2-Clause
#
# A method belongs to the class that defines it.
#
# parse_defs built one flat hash per FILE, keyed by method name, so
# two package classes sharing a file shared a namespace and the later
# definition won. ncurses.rb held a single install_impl_internal and
# it was NcursesHostPackage's, so class_source(NcursesPackage)
# returned the host package's build. The target's recipe could then be
# rewritten without changing the target's digest: a changed recipe
# going unnoticed, which is the one failure this mechanism exists to
# prevent.
#
# It cannot be caught by reading a digest, only by asking whose code
# is in it -- which is what these do.
#

require_relative 'test_helper'
require_relative '../main'
require_relative '../source_digest'

class TestSourceDigestIsPerClass < Minitest::Test

  include TestHelper

  # The real pair that exposed it: two packages, one file, one method
  # name, two genuinely different builds.
  def test_each_class_gets_its_own_method_body
    target = SourceDigest.class_source(NcursesPackage)
    host   = SourceDigest.class_source(NcursesHostPackage)

    assert_includes target, "--host=",
                    "the target package lost its own build"
    assert_includes host, "--with-termlib",
                    "the host package lost its own build"

    refute_includes target, "--with-termlib",
                    "the target package is fingerprinting the HOST's build"
    refute_includes host, "--host=",
                    "the host package is fingerprinting the TARGET's build"
  end

  # Stated generally: no two classes of one file may hand back the
  # same text for a method they both define.
  def test_no_class_borrows_a_neighbours_method
    by_file = Hash.new { |h, k| h[k] = [] }

    for p in pkgmgr.all_packages do
      f = SourceDigest.source_file_of(p.class)
      by_file[f] << p.class if f
    end

    for f, classes in by_file do
      classes = classes.uniq
      next if classes.length < 2

      for a, b in classes.combination(2) do
        shared = a.instance_methods(false) & b.instance_methods(false)
        next if shared.empty?

        sa = SourceDigest.class_source(a)
        sb = SourceDigest.class_source(b)

        for m in shared do
          ta = SourceDigest.method_defs(f)[SourceDigest.scope_of(a)][m]
          tb = SourceDigest.method_defs(f)[SourceDigest.scope_of(b)][m]
          refute_nil ta, "#{a}##{m} has no source of its own"
          refute_nil tb, "#{b}##{m} has no source of its own"
        end
      end
    end
  end

  # Asking for a name defined twice in a file is ambiguous, and the
  # answer is to say so. Picking one is how this went unnoticed.
  def test_an_ambiguous_method_name_is_refused
    f = SourceDigest.source_file_of(NcursesPackage)
    err = assert_raises(RuntimeError) {
      SourceDigest.method_source(f, :install_impl_internal)
    }
    assert_match(/defined in 2 classes/, err.message)
  end

  # ...and an unambiguous one still answers. The build helpers are
  # looked up this way.
  def test_a_unique_method_name_still_resolves
    f = SourceDigest.source_file_of(Package)
    src = SourceDigest.method_source(f, :meson_stack_build)
    assert_includes src, "meson"
  end
end
