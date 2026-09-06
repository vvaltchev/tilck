# SPDX-License-Identifier: BSD-2-Clause
#
# TWO OF EVERYTHING, ONE AXIS AT A TIME.
#
# More than half the bugs this package manager has had are one bug:
# an operation that acts on a specific installation, performed without
# saying which one it means. find_install comparing versions instead
# of Coords; force_remove taking every board of an arch, then every
# version of a package; the staleness check reading a stack package's
# flags at whichever stack happened to be current; -f removing a tree
# nobody was rebuilding. Each was invisible because the wrong answer
# is a plausible one -- the code returns AN install, just not the one
# asked for.
#
# They survive because a fixture with one install cannot tell "the
# right one" from "the only one". So this file is a matrix: for every
# axis an installation can differ along, two REAL installs that differ
# in exactly that and nothing else, and every operation asked to touch
# one of them.
#
#   axis      the two installs differ in
#   -------   ---------------------------------------------
#   version   1.0.0 and 2.0.0, same coordinates
#   arch      i386 and x86_64
#   board     riscv64/qemu-virt and riscv64/licheerv-nano
#   stack     gcc-<A> and gcc-<B>, a host :stack package
#
# Real installs, not mkdir_p: a bare directory carries no
# .build_inputs, so it cannot answer a staleness question, and half
# the operations here are about exactly that.
#

require_relative 'test_helper'

class TestCoordinateMatrix < Minitest::Test

  include TestHelper

  V1 = Ver("1.0.0")
  V2 = Ver("2.0.0")

  STACK_A = Ver("7.7.7")
  STACK_B = Ver("8.8.8")

  # What an axis hands back: the package, the two installs, and a way
  # to open the ambient context each of them lives in. `scope` is what
  # every one of these operations reads implicitly when nobody tells
  # it otherwise -- which is the whole subject.
  Pair = Struct.new(:pkg, :a, :b, :scope_a, :scope_b, :ver_a, :ver_b,
                    keyword_init: true)

  def inst_of(pkg, ver, &scope)
    scope.call {
      pkg.get_install_list.find { |i|
        i.ver == ver && !i.path.nil? && i.coords == pkg.coords(ver)
      }
    }
  end

  def here = ->(&blk) { blk.call }

  # --- the axes --------------------------------------------------------

  def axis_version
    pkg = FakePackage.new("multi")
    pkgmgr.register(pkg)
    pkgmgr.install("multi", V1)
    pkgmgr.install("multi", V2)
    pkgmgr.refresh

    Pair.new(
      pkg: pkg, ver_a: V1, ver_b: V2,
      a: inst_of(pkg, V1, &here), b: inst_of(pkg, V2, &here),
      scope_a: here, scope_b: here,
    )
  end

  def axis_arch
    pkg = FakePackage.new("archy")
    pkgmgr.register(pkg)

    i386 = ->(&blk) { with_context(ARCH: ALL_ARCHS["i386"], BOARD: nil, &blk) }
    x64  = ->(&blk) { with_context(ARCH: ALL_ARCHS["x86_64"], BOARD: nil, &blk) }

    i386.call { pkgmgr.install("archy") }
    x64.call  { pkgmgr.install("archy") }
    pkgmgr.refresh

    Pair.new(
      pkg: pkg, ver_a: V1, ver_b: V1,
      a: inst_of(pkg, V1, &i386), b: inst_of(pkg, V1, &x64),
      scope_a: i386, scope_b: x64,
    )
  end

  def axis_board
    pkg = FakePackage.new("boardy")
    pkgmgr.register(pkg)

    rv = ALL_ARCHS["riscv64"]
    qemu = ->(&blk) { with_context(ARCH: rv, BOARD: "qemu-virt", &blk) }
    lichee = ->(&blk) { with_context(ARCH: rv, BOARD: "licheerv-nano", &blk) }

    qemu.call   { pkgmgr.install("boardy") }
    lichee.call { pkgmgr.install("boardy") }
    pkgmgr.refresh

    Pair.new(
      pkg: pkg, ver_a: V1, ver_b: V1,
      a: inst_of(pkg, V1, &qemu), b: inst_of(pkg, V1, &lichee),
      scope_a: qemu, scope_b: lichee,
    )
  end

  def axis_stack
    pkg = FakePackage.new("host_stacky", on_host: true,
                          host_tier: :stack,
                          arch_list: ALL_HOST_ARCHS.values)
    pkgmgr.register(pkg)

    sa = ->(&blk) { pkgmgr.with_host_stack(STACK_A, &blk) }
    sb = ->(&blk) { pkgmgr.with_host_stack(STACK_B, &blk) }

    sa.call { pkgmgr.install("host_stacky") }
    sb.call { pkgmgr.install("host_stacky") }
    pkgmgr.refresh

    Pair.new(
      pkg: pkg, ver_a: V1, ver_b: V1,
      a: inst_of(pkg, V1, &sa), b: inst_of(pkg, V1, &sb),
      scope_a: sa, scope_b: sb,
    )
  end

  AXES = [:version, :arch, :board, :stack].freeze

  def with_axis(axis)
    with_fake_tc do
      with_stubbed_externals do
        reset_pkgmgr!
        p = send("axis_#{axis}")

        refute_nil p.a, "#{axis}: the first install did not happen"
        refute_nil p.b, "#{axis}: the second install did not happen"
        refute_equal p.a.path, p.b.path,
                     "#{axis}: both installs landed in one place, so " \
                     "this axis tests nothing"

        yield p
      end
    end
  end

  # --- the operations, once per axis -----------------------------------

  AXES.each do |axis|

    # The lookup everything else is built on.
    define_method("test_#{axis}_find_install_returns_its_own") do
      with_axis(axis) do |p|
        found = p.scope_a.call { p.pkg.find_install(p.ver_a) }
        refute_nil found, "#{axis}: found nothing in its own scope"
        assert_equal p.a.path, found.path,
                     "#{axis}: find_install answered about the other one"
      end
    end

    # -u removes what the user is looking at, and nothing else.
    define_method("test_#{axis}_uninstall_leaves_the_other_alone") do
      with_axis(axis) do |p|
        p.scope_a.call {
          pkgmgr.uninstall(p.pkg.name, false, false, p.ver_a)
        }
        pkgmgr.refresh

        refute p.a.path.directory?, "#{axis}: -u removed nothing"
        assert p.b.path.directory?, "#{axis}: -u took the other one too"
      end
    end

    # -f removes exactly the tree the install is about to recreate.
    define_method("test_#{axis}_force_remove_leaves_the_other_alone") do
      with_axis(axis) do |p|
        p.scope_a.call { pkgmgr.force_remove(p.pkg.name, p.ver_a) }
        pkgmgr.refresh

        refute p.a.path.directory?, "#{axis}: -f removed nothing"
        assert p.b.path.directory?, "#{axis}: -f took the other one too"
      end
    end

    # A recipe is only a recipe AT some coordinates. Judging one
    # install from the other's scope has to give the same answer as
    # judging it from its own, or a package built minutes ago reads
    # as stale.
    define_method("test_#{axis}_staleness_is_judged_where_it_lives") do
      with_axis(axis) do |p|
        from_own = p.scope_b.call { p.pkg.build_inputs_state_of(p.b) }
        from_other = p.scope_a.call { p.pkg.build_inputs_state_of(p.b) }

        assert_equal :ok, from_own,
                     "#{axis}: a fresh install is not ok in its own scope"
        assert_equal from_own, from_other,
                     "#{axis}: judged differently from the other scope"
      end
    end

    # Both are installs of one package, and the listing has to show
    # both however the invocation is scoped.
    define_method("test_#{axis}_both_are_listed") do
      with_axis(axis) do |p|
        paths = p.scope_a.call {
          p.pkg.get_install_list.reject { |i| i.path.nil? }.map(&:path)
        }

        assert_includes paths, p.a.path, "#{axis}: its own is missing"
        assert_includes paths, p.b.path, "#{axis}: the other is missing"
      end
    end
  end
end
