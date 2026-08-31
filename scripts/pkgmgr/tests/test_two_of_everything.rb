# SPDX-License-Identifier: BSD-2-Clause
#
# Every identity bug this package manager has had was "two of X".
#
# Not an unexecuted line -- coordinate handling sits at 89-100% line
# coverage and always did. Each bug was a question answered about the
# wrong ONE of two things that existed at once:
#
#   two boards of an arch     one board's install answered for the other
#   two arches of a version   one arch's recipe was recorded for both
#   two versions of a package -f removed all of them to rebuild one
#   two stacks                composing B's sysroot read A's install
#   two classes in a file     one class's method was hashed for the other
#   two packages, one source  one package's patches reached the other
#
# In every case the suite had exactly one of the thing. So this file
# does nothing but put two of each axis in place and ask who answers.
# It deliberately overlaps the regression tests: those pin the bugs
# that happened, this states the property they were instances of, and
# covers pairs no bug has reached yet.
#

require_relative 'test_helper'

class TestTwoOfEverything < Minitest::Test

  include TestHelper

  RV = ALL_ARCHS["riscv64"]      # the only arch with two boards
  I386 = ALL_ARCHS["i386"]
  V1 = Ver("1.0.0")
  V2 = Ver("2.0.0")
  OTHER_STACK = "13.4.0"

  def mk(name, **kw) = FakePackage.new(name, **kw)

  def touch(dir)
    FileUtils.mkdir_p(dir)
    return dir
  end

  # --- two boards of one arch ----------------------------------------

  def test_two_boards_two_installs
    with_fake_tc do
      reset_pkgmgr!
      pkg = mk("p", arch_list: [RV])
      pkgmgr.register(pkg)

      for b in RV.boards do
        touch(target_pkgs(RV, FAKE_GCC_VER.to_s, b) / "p" / V1.to_s)
      end
      pkgmgr.refresh()

      found = pkg.get_install_list.map { |i| i.coords.env }.sort
      assert_equal RV.boards.sort, found

      for b in RV.boards do
        with_context(ARCH: RV, BOARD: b) do
          assert_equal b, pkg.find_install(V1).coords.env,
                       "board #{b} resolved to another board's install"
        end
      end
    end
  end

  # --- two arches of one version --------------------------------------

  def test_two_arches_are_separate_installs
    with_fake_tc do
      reset_pkgmgr!
      pkg = mk("p", arch_list: [I386, RV])
      pkgmgr.register(pkg)

      touch(target_pkgs(I386, FAKE_GCC_VER.to_s) / "p" / V1.to_s)
      touch(target_pkgs(RV, FAKE_GCC_VER.to_s) / "p" / V1.to_s)
      pkgmgr.refresh()

      for a in [I386, RV] do
        with_context(ARCH: a, BOARD: nil) do
          assert_equal "tilck-#{a.name}", pkg.find_install(V1).coords.machine,
                       "#{a.name} resolved to another arch's install"
        end
      end
    end
  end

  # --- two versions of one package ------------------------------------

  def test_two_versions_are_separate_installs
    with_fake_tc do
      reset_pkgmgr!
      pkg = mk("p")
      pkgmgr.register(pkg)

      base = target_pkgs(ARCH, FAKE_GCC_VER.to_s) / "p"
      touch(base / V1.to_s)
      touch(base / V2.to_s)
      pkgmgr.refresh()

      assert_equal V1, pkg.find_install(V1).ver
      assert_equal V2, pkg.find_install(V2).ver
      assert_equal 2, pkg.get_install_list.length
    end
  end

  # --- two stacks -------------------------------------------------------

  def test_two_stacks_are_separate_installs
    with_fake_tc do
      reset_pkgmgr!
      pkg = mk("host_p", on_host: true, host_tier: :stack,
               arch_list: ALL_HOST_ARCHS.values)
      pkgmgr.register(pkg)

      default = pkgmgr.default_stack_cc_ver.to_s

      for s in [default, OTHER_STACK] do
        pkgmgr.with_host_stack(Ver(s)) do
          touch(pkg.coords.pkgs_dir / "p" / V1.to_s)
        end
      end
      pkgmgr.refresh()

      for s in [default, OTHER_STACK] do
        pkgmgr.with_host_stack(Ver(s)) do
          assert_equal "gcc-#{s}", pkg.find_install(V1).coords.stack,
                       "stack #{s} resolved to another stack's install"
        end
      end
    end
  end

  # A stack package at two versions in two stacks: four installs, and
  # each of the four coordinates must select exactly one.
  def test_two_stacks_times_two_versions
    with_fake_tc do
      reset_pkgmgr!
      pkg = mk("host_p", on_host: true, host_tier: :stack,
               arch_list: ALL_HOST_ARCHS.values)
      pkgmgr.register(pkg)

      stacks = [pkgmgr.default_stack_cc_ver.to_s, OTHER_STACK]

      for s in stacks do
        pkgmgr.with_host_stack(Ver(s)) do
          touch(pkg.coords.pkgs_dir / "p" / V1.to_s)
          touch(pkg.coords.pkgs_dir / "p" / V2.to_s)
        end
      end
      pkgmgr.refresh()

      assert_equal 4, pkg.get_install_list.length

      for s in stacks do
        for v in [V1, V2] do
          pkgmgr.with_host_stack(Ver(s)) do
            i = pkg.find_install(v)
            assert_equal v, i.ver
            assert_equal "gcc-#{s}", i.coords.stack
          end
        end
      end
    end
  end

  # --- two classes in one file ------------------------------------------

  def test_two_classes_in_one_file_keep_their_own_methods
    # ncurses.rb is the real pair: same file, same method names, two
    # genuinely different builds.
    require_relative '../ncurses'

    t = SourceDigest.class_source(NcursesPackage)
    h = SourceDigest.class_source(NcursesHostPackage)

    refute_equal t, h, "two classes in one file hash identically"
  end

end
