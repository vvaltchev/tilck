# SPDX-License-Identifier: BSD-2-Clause
#
# THE SCOPE, AND A PACKAGE BOUND TO ONE.
#
# A scoped question -- where does this install, which arch is it
# built for, which stack does it belong to -- is answered under a
# Scope, and the scope is either bound to the package by the caller
# (Package#at) or, during the transition, the invocation's. These
# pin the three rules of the value and the two properties of binding
# that the conversion rests on.
#

require_relative 'test_helper'

class TestScope < Minitest::Test

  include TestHelper

  I386 = ALL_ARCHS["i386"]
  RV   = ALL_ARCHS["riscv64"]

  def env(stack: Ver("7.7.7")) = Scope.env(stack: stack)

  # --- the value ---------------------------------------------------------

  def test_the_environment_scope_is_the_shell_pair
    with_context(ARCH: RV, BOARD: "licheerv-nano") do
      s = env
      assert_equal RV, s.arch
      assert_equal "licheerv-nano", s.board
      assert_equal RV, s.env_arch
      assert_equal "licheerv-nano", s.env_board
      assert_equal Ver("7.7.7"), s.stack
    end
  end

  def test_a_blank_board_is_the_arch_default
    with_context(ARCH: RV, BOARD: nil) do
      assert_equal RV.default_board, env.board
      assert_nil env.env_board
    end
  end

  # The one rule about boards, in its three cases.
  def test_board_of_the_scoped_arch_the_shell_arch_and_another
    with_context(ARCH: I386, BOARD: "pc") do
      s = env.with(arch: RV, board: "licheerv-nano")
      assert_equal "licheerv-nano", s.board_of(RV),  "the scoped board"
      assert_equal "pc", s.board_of(I386),            "the shell's BOARD"
      s2 = s.with(arch: I386)
      assert_equal "pc", s2.board
      assert_equal RV.default_board, s2.board_of(RV), "an arch's default"
    end
  end

  # A board opened for one arch does not leak onto another arch
  # opened inside it: the quirk the ivar scoping had.
  def test_an_inner_arch_gets_its_own_board_not_the_outer_ones
    with_context(ARCH: I386, BOARD: "pc") do
      outer = env.with(arch: RV, board: "licheerv-nano")
      inner = outer.with(arch: I386)
      assert_equal "pc", inner.board
      back = inner.with(arch: RV)
      assert_equal RV.default_board, back.board,
                   "the outer board is gone once the arch moved"
    end
  end

  def test_with_stack_keeps_the_arch_and_the_board
    with_context(ARCH: RV, BOARD: "licheerv-nano") do
      s = env.with(stack: Ver("8.8.8"))
      assert_equal [RV, "licheerv-nano", Ver("8.8.8")],
                   [s.arch, s.board, s.stack]
    end
  end

  # --- the environment's scope --------------------------------------------

  # Deriving a scope from the environment's moves one field and leaves
  # the environment's as it was: there is nothing to put back.
  def test_a_derived_scope_leaves_the_environments_as_it_was
    with_context(ARCH: I386, BOARD: "pc") do
      reset_pkgmgr!
      before = scope
      sc = scope.with(arch: RV, board: "licheerv-nano")
      assert_equal RV, sc.arch
      assert_equal "licheerv-nano", sc.board_of(RV)
      sc2 = sc.with(stack: Ver("8.8.8"))
      assert_equal Ver("8.8.8"), sc2.stack
      assert_equal RV, sc2.arch
      assert_equal before.stack, sc.stack
      assert_equal before, scope
    end
  end

  # What HOST_VER_GCC says is what the environment's scope names; a
  # test may say otherwise, for a block or for good.
  def test_the_configured_stack_names_the_environments
    reset_pkgmgr!
    pkgmgr.default_stack = Ver("9.9.9")
    assert_equal Ver("9.9.9"), scope.stack
    with_host_stack(Ver("8.8.8")) {
      assert_equal Ver("8.8.8"), scope.stack
    }
    assert_equal Ver("9.9.9"), scope.stack
  ensure
    reset_pkgmgr!
  end

  # --- binding -------------------------------------------------------------

  def test_a_bound_package_answers_from_its_scope_and_nothing_else
    with_context(ARCH: I386, BOARD: "pc") do
      with_fake_tc do
        reset_pkgmgr!
        pkg = FakePackage.new("t", arch_list: [I386, RV])
        pkgmgr.register(pkg)
        s = scope.with(arch: RV, board: "licheerv-nano")

        b = pkg.at(s)
        assert b.bound?
        refute pkg.bound?, "binding is a copy; the registry package stays"
        assert_equal RV, b.default_arch
        assert_equal "tilck-riscv64", b.coords.machine
        assert_equal "licheerv-nano", b.coords.env

        # The invocation's scope is untouched by a binding.
        assert_equal I386, bound(pkg).default_arch
        assert_equal "pc", bound(pkg).coords.env
      end
    end
  end

  def test_binding_twice_is_the_same_answer
    with_fake_tc do
      reset_pkgmgr!
      pkg = FakePackage.new("t")
      s = scope
      assert_equal pkg.at(s).coords, pkg.at(s).coords
      assert_equal pkg.at(s).install_dir(pkg.default_ver),
                   pkg.at(s).install_dir(pkg.default_ver)
    end
  end

  def test_binding_wants_a_scope
    assert_raises(ArgumentError) { FakePackage.new("t").at(I386) }
  end

  # The Tilck stack's members are asked at the stack's own coordinates,
  # through binding: the first scoped question with no block around it.
  def test_a_tilck_stack_asks_its_members_bound_to_its_own_coordinates
    with_context(ARCH: I386, BOARD: "pc") do
      with_fake_tc do
        reset_pkgmgr!
        pkgmgr.register(FakePackage.new("rv_only", arch_list: [RV],
                                        default: true))
        pkgmgr.register(FakePackage.new("i386_only", arch_list: [I386],
                                        default: true))
        stack = TilckStackPackage.new(RV, "qemu-virt")
        pkgmgr.register(stack)
        assert_equal ["rv_only"], bound(stack).dep_list.map(&:name)
      end
    end
  end

  # An arch with no boards installs under the env ANY, and an install
  # there scopes to its arch at no board -- which is what the arch
  # answers for its default board too.
  def test_an_install_of_a_boardless_arch_scopes_to_no_board
    a64 = ALL_ARCHS["aarch64"]
    assert_nil a64.boards, "the fixture: aarch64 declares no boards"
    with_fake_tc do
      reset_pkgmgr!
      t = FakePackage.new("t", arch_list: [a64])
      pkgmgr.register(t)
      fake_install(t, at: t.at(scope.with(arch: a64)).coords)
      inst = pkgmgr.world.of("t").first
      refute_nil inst, "the scan finds an install under the env ANY"
      assert_equal Coords::ANY, inst.coords.env
      sc = t.at(scope).scope_at(inst)
      assert_equal [a64, a64.default_board], [sc.arch, sc.board]
    end
  end
end
