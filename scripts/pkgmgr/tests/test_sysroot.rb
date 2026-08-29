# SPDX-License-Identifier: BSD-2-Clause
#
# The composed sysroot. Pure filesystem work, so these build the
# fragments out of temp directories.
#

require_relative 'test_helper'
require_relative '../sysroot'

class TestSysrootCompose < Minitest::Test

  def frag(root, name, files)
    d = File.join(root, name)
    files.each do |rel, content|
      path = File.join(d, rel)
      FileUtils.mkdir_p(File.dirname(path))
      File.write(path, content)
    end
    return d
  end

  def test_files_become_symlinks_to_their_fragment
    Dir.mktmpdir do |dir|
      a = frag(dir, "a", { "usr/lib/libfoo.so" => "x" })
      target = File.join(dir, "sysroot")

      Sysroot.compose(target, [a])

      link = File.join(target, "usr/lib/libfoo.so")
      assert File.symlink?(link)
      assert_equal File.join(a, "usr/lib/libfoo.so"), File.readlink(link)
      assert_equal "x", File.read(link)
    end
  end

  def test_directories_are_real_not_linked
    Dir.mktmpdir do |dir|
      a = frag(dir, "a", { "usr/lib/libfoo.so" => "x" })
      target = File.join(dir, "sysroot")

      Sysroot.compose(target, [a])

      d = File.join(target, "usr/lib")
      assert File.directory?(d)
      refute File.symlink?(d)
    end
  end

  # The point of mirroring directories rather than linking them: two
  # packages contribute to the same usr/lib.
  def test_two_fragments_merge_into_one_tree
    Dir.mktmpdir do |dir|
      a = frag(dir, "a", { "usr/lib/liba.so" => "a",
                           "usr/include/a.h" => "a" })
      b = frag(dir, "b", { "usr/lib/libb.so" => "b",
                           "usr/include/b.h" => "b" })
      target = File.join(dir, "sysroot")

      Sysroot.compose(target, [a, b])

      assert_equal "a", File.read(File.join(target, "usr/lib/liba.so"))
      assert_equal "b", File.read(File.join(target, "usr/lib/libb.so"))
      assert_equal "a", File.read(File.join(target, "usr/include/a.h"))
      assert_equal "b", File.read(File.join(target, "usr/include/b.h"))
    end
  end

  def test_deeply_nested_paths
    Dir.mktmpdir do |dir|
      a = frag(dir, "a", { "usr/include/sys/deep/nested/x.h" => "x" })
      target = File.join(dir, "sysroot")

      Sysroot.compose(target, [a])
      assert_equal "x",
                   File.read(File.join(target, "usr/include/sys/deep/nested/x.h"))
    end
  end

  # Which of the two won would depend on install order, so it is an
  # error rather than a silent last-one-wins.
  def test_same_file_from_two_fragments_is_an_error
    Dir.mktmpdir do |dir|
      a = frag(dir, "a", { "usr/lib/libc.so" => "a" })
      b = frag(dir, "b", { "usr/lib/libc.so" => "b" })
      target = File.join(dir, "sysroot")

      e = assert_raises(Sysroot::ConflictError) {
        Sysroot.compose(target, [a, b])
      }
      assert_match(%r{usr/lib/libc\.so}, e.message)
      assert_match(/#{Regexp.escape(a)}/, e.message)
      assert_match(/#{Regexp.escape(b)}/, e.message)
    end
  end

  # A stale link from a version no longer selected is the failure mode
  # hardest to notice, so composition rebuilds rather than updates.
  def test_composition_drops_entries_from_a_previous_run
    Dir.mktmpdir do |dir|
      old = frag(dir, "old", { "usr/lib/libold.so" => "old" })
      new = frag(dir, "new", { "usr/lib/libnew.so" => "new" })
      target = File.join(dir, "sysroot")

      Sysroot.compose(target, [old])
      assert File.exist?(File.join(target, "usr/lib/libold.so"))

      Sysroot.compose(target, [new])
      refute File.exist?(File.join(target, "usr/lib/libold.so"))
      assert File.exist?(File.join(target, "usr/lib/libnew.so"))
    end
  end

  def test_missing_fragment_is_skipped
    Dir.mktmpdir do |dir|
      a = frag(dir, "a", { "usr/lib/liba.so" => "a" })
      target = File.join(dir, "sysroot")

      Sysroot.compose(target, [a, File.join(dir, "nope")])
      assert File.exist?(File.join(target, "usr/lib/liba.so"))
    end
  end

  def test_no_fragments_gives_an_empty_sysroot
    Dir.mktmpdir do |dir|
      target = File.join(dir, "sysroot")
      assert_equal 0, Sysroot.compose(target, [])
      assert File.directory?(target)
    end
  end

  def test_returns_the_number_of_links
    Dir.mktmpdir do |dir|
      a = frag(dir, "a", { "usr/lib/x.so" => "x", "usr/include/y.h" => "y" })
      assert_equal 2, Sysroot.compose(File.join(dir, "sysroot"), [a])
    end
  end

  def test_dotfiles_are_included
    Dir.mktmpdir do |dir|
      a = frag(dir, "a", { "usr/lib/.hidden" => "h" })
      target = File.join(dir, "sysroot")

      Sysroot.compose(target, [a])
      assert_equal "h", File.read(File.join(target, "usr/lib/.hidden"))
    end
  end
end

#
# Grafted fragments: a package whose own layout is not sysroot-shaped
# contributing part of itself anyway. GCC keeps libstdc++ and libgcc_s
# in lib64/ and is not a hermetic package, but that runtime is built
# against our glibc and has to be in the sysroot or every C++ binary it
# produces dies at startup.
#
class TestSysrootGraftedFragments < Minitest::Test

  def frag(root, name, files)
    d = File.join(root, name)
    files.each do |rel, content|
      path = File.join(d, rel)
      FileUtils.mkdir_p(File.dirname(path))
      File.write(path, content)
    end
    return d
  end

  def test_a_pair_grafts_at_the_given_subpath
    Dir.mktmpdir do |dir|
      lib64 = frag(dir, "gcc/lib64", { "libstdc++.so.6" => "cxx" })
      target = File.join(dir, "sysroot")

      Sysroot.compose(target, [[lib64, "usr/lib"]])

      assert_equal "cxx",
                   File.read(File.join(target, "usr/lib/libstdc++.so.6"))
    end
  end

  def test_grafted_and_plain_fragments_merge
    Dir.mktmpdir do |dir|
      libc = frag(dir, "glibc", { "usr/lib/libc.so.6" => "libc" })
      lib64 = frag(dir, "gcc/lib64", { "libstdc++.so.6" => "cxx" })
      target = File.join(dir, "sysroot")

      Sysroot.compose(target, [libc, [lib64, "usr/lib"]])

      assert_equal "libc", File.read(File.join(target, "usr/lib/libc.so.6"))
      assert_equal "cxx",
                   File.read(File.join(target, "usr/lib/libstdc++.so.6"))
    end
  end

  # Ownership is keyed by the path in the SYSROOT, not in the fragment,
  # or two fragments grafted to the same place would collide unnoticed.
  def test_a_graft_colliding_with_a_plain_fragment_is_an_error
    Dir.mktmpdir do |dir|
      a = frag(dir, "a", { "usr/lib/libfoo.so" => "a" })
      b = frag(dir, "b/lib64", { "libfoo.so" => "b" })
      target = File.join(dir, "sysroot")

      assert_raises(Sysroot::ConflictError) {
        Sysroot.compose(target, [a, [b, "usr/lib"]])
      }
    end
  end

  def test_two_grafts_to_different_places_do_not_collide
    Dir.mktmpdir do |dir|
      a = frag(dir, "a", { "libfoo.so" => "a" })
      b = frag(dir, "b", { "libfoo.so" => "b" })
      target = File.join(dir, "sysroot")

      Sysroot.compose(target, [[a, "usr/lib"], [b, "usr/lib32"]])
      assert_equal "a", File.read(File.join(target, "usr/lib/libfoo.so"))
      assert_equal "b", File.read(File.join(target, "usr/lib32/libfoo.so"))
    end
  end

  def test_a_missing_graft_source_is_skipped
    Dir.mktmpdir do |dir|
      a = frag(dir, "a", { "usr/lib/liba.so" => "a" })
      target = File.join(dir, "sysroot")

      Sysroot.compose(target, [a, [File.join(dir, "nope"), "usr/lib"]])
      assert File.exist?(File.join(target, "usr/lib/liba.so"))
    end
  end
end
