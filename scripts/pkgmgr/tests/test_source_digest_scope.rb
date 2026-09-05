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
    target = SourceDigest.own_source(NcursesPackage)
    host   = SourceDigest.own_source(NcursesHostPackage)

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

  # A recipe shared by several packages lives on their common parent,
  # and hashing only the leaf class missed it completely.
  #
  # gmp, mpfr, mpc and isl are one build written once on
  # GccPrereqPackage; their own classes hold a name and a file list.
  # So the fingerprint covered everything about them EXCEPT how they
  # are built -- verified before the fix by adding --disable-assembly
  # to that shared configure, which changes the binaries gmp produces,
  # and watching --check-for-updates stay silent about all four.
  def test_a_recipe_on_a_shared_parent_is_part_of_the_digest
    own = SourceDigest.own_source(HostGmpPackage)
    full = SourceDigest.class_source(HostGmpPackage, upto: Package)

    refute_includes own, "--enable-static",
                    "the leaf class was expected to hold no recipe"
    assert_includes full, "--enable-static",
                    "the shared parent's build is not in the digest"
  end

  # The walk stops at Package: its build helpers are hashed
  # separately, and only for the packages that actually call them.
  def test_the_walk_stops_at_the_framework
    full = SourceDigest.class_source(HostGmpPackage, upto: Package)
    refute_includes full, "def meson_stack_build",
                    "the framework's own methods leaked into a package"
  end

  # ...and it stays precise: widening the digest to the parent chain
  # must not make unrelated packages share a fingerprint.
  def test_two_families_do_not_borrow_each_others_parents
    gmp = SourceDigest.class_source(HostGmpPackage, upto: Package)
    qemu = SourceDigest.class_source(HostQemuPackage, upto: Package)

    refute_equal gmp, qemu
    refute_includes qemu, "--enable-static"
  end

  # What a package IS, and whether it may be asked for, is not how it
  # is built: excluded, for the same reason comments are.
  def test_the_non_recipe_hooks_are_left_out
    kept = SourceDigest.class_source(Acpica, upto: Package)
    dropped = SourceDigest.class_source(Acpica, upto: Package,
                                        except: Package::NON_RECIPE_HOOKS)

    assert_includes kept, "def default_cc",
                    "the fixture no longer defines the hook it is about"
    refute_includes dropped, "def default_cc"
  end
end
