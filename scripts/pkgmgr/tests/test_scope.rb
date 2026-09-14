# SPDX-License-Identifier: BSD-2-Clause
#
# THE SCOPE, AND A PACKAGE BOUND TO ONE.
#
# A scoped question -- where does this install, which arch is it
# built for, which stack does it belong to -- is answered under a
# Scope, and the scope is either bound to the package by the caller
# (Package#at) or, during the transition, the invocation's. These
# pin the three rules of the value and the two properties of binding
# that the conversion rests on, and hold the transition's count of
# unbound reads to what the last step left.
#

require_relative 'test_helper'

class TestScope < Minitest::Test

  include TestHelper

  I386 = ALL_ARCHS["i386"]
  RV   = ALL_ARCHS["riscv64"]

  # TRANSITION: the number of distinct sites in the CORE files that
  # asked an unbound package a scoped question in the last full run.
  # tests/run_all.rb fails a run that exceeds it. Each step of the
  # conversion lowers it and records the new number in its commit
  # message; step 5.6 sets it to zero and deletes the fallback.
  #
  # The core is what decides: the manager, the CLI, the base class
  # and the three packages that carry logic of their own. Every
  # other product file is a recipe, converted wholesale when the
  # executor binds the package it builds; the harness files are
  # converted with the tests.
  UNBOUND_CEILING = 15

  # The same, for a package asked what is installed with no World in
  # hand (step 5.2): the manager's scan answers, and the site counts.
  UNBOUND_WORLD_CEILING = 14

  # The same, for a package asked which version a request bound with
  # no Plan in hand (step 5.3): the rebuild's stash answers.
  UNBOUND_VERSION_CEILING = 1
  CORE = %w[package.rb package_manager.rb main.rb layout.rb recipe.rb
            scope.rb coords.rb install_selector.rb dep_resolver.rb
            version_solver.rb build_inputs.rb build_env.rb
            system_deps.rb early_logic.rb gcc.rb host_gcc.rb
            tilck_stack.rb world.rb plan.rb planner.rb
            executor.rb].freeze
  HARNESS = %w[test_helper.rb bridge.rb model.rb laws.rb runner.rb
               domain.rb].freeze

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

  # --- the manager's openers, now the same value -----------------------

  def test_the_openers_move_one_field_each_and_put_it_back
    with_context(ARCH: I386, BOARD: "pc") do
      reset_pkgmgr!
      before = pkgmgr.scope
      pkgmgr.with_target_coords(RV, "licheerv-nano") do
        assert_equal RV, pkgmgr.target_arch
        assert_equal "licheerv-nano", pkgmgr.board_for(RV)
        pkgmgr.with_host_stack(Ver("8.8.8")) do
          assert_equal Ver("8.8.8"), pkgmgr.current_host_stack
          assert_equal RV, pkgmgr.target_arch
        end
        assert_equal before.stack, pkgmgr.current_host_stack
      end
      assert_equal before, pkgmgr.scope
    end
  end

  def test_minus_h_names_the_stack_for_the_whole_invocation
    reset_pkgmgr!
    pkgmgr.host_stack = Ver("9.9.9")
    assert_equal Ver("9.9.9"), pkgmgr.current_host_stack
    pkgmgr.with_host_stack(Ver("8.8.8")) {
      assert_equal Ver("8.8.8"), pkgmgr.current_host_stack
    }
    assert_equal Ver("9.9.9"), pkgmgr.current_host_stack
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
        s = pkgmgr.scope.with(arch: RV, board: "licheerv-nano")

        b = pkg.at(s)
        assert b.bound?
        refute pkg.bound?, "binding is a copy; the registry package stays"
        assert_equal RV, b.default_arch
        assert_equal "tilck-riscv64", b.coords.machine
        assert_equal "licheerv-nano", b.coords.env

        # The invocation's scope is untouched by a binding.
        assert_equal I386, pkg.default_arch
        assert_equal "pc", pkg.coords.env
      end
    end
  end

  def test_binding_twice_is_the_same_answer
    with_fake_tc do
      reset_pkgmgr!
      pkg = FakePackage.new("t")
      s = pkgmgr.scope
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
        assert_equal ["rv_only"], stack.dep_list.map(&:name)
      end
    end
  end
end
