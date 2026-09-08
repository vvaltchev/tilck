# SPDX-License-Identifier: BSD-2-Clause
#
# THE TILCK STACKS: one meta-package per target, whose dependencies
# are the default set of that arch and board, installed by the no-mode
# run, holding its members against --autoremove.
#

require_relative 'test_helper'
require_relative '../tilck_stack'

class TestTilckStack < Minitest::Test

  include TestHelper

  I386 = ALL_ARCHS["i386"]
  RV   = ALL_ARCHS["riscv64"]

  def setup = reset_pkgmgr!

  # The real packages, in a fake tree: what the stacks are made of is
  # what the packages declare, and nothing here reads an install.
  def with_the_real_packages
    with_fake_tc do
      REAL_PACKAGES.each { |p| pkgmgr.register(p) }
      yield
    end
  end

  def test_one_stack_per_arch_and_board
    with_the_real_packages do
      names = pkgmgr.tilck_stacks.map(&:name).sort
      assert_equal %w[tilck-i386-pc tilck-riscv64-licheerv-nano
                      tilck-riscv64-qemu-virt tilck-x86_64-pc], names
      assert pkgmgr.tilck_stacks.all?(&:metapackage?)
    end
  end

  # The members are what the packages themselves declare default under
  # the stack's own coordinates: x86 takes both of its compilers, the
  # UEFI loader being 64-bit whatever the kernel is; riscv64 one, and
  # its board's boot.
  def test_the_members_are_the_defaults_of_the_stacks_own_target
    with_the_real_packages do
      i386 = pkgmgr.get("tilck-i386-pc").dep_list.map(&:name)
      assert_includes i386, "gcc-i386-musl"
      assert_includes i386, "gcc-x86_64-musl"
      assert_includes i386, "gnuefi"
      refute_includes i386, "gcc-riscv64-musl"
      refute_includes i386, "uboot"

      nano = pkgmgr.get("tilck-riscv64-licheerv-nano").dep_list.map(&:name)
      assert_includes nano, "gcc-riscv64-musl"
      assert_includes nano, "licheerv_nano_boot"
      refute_includes nano, "uboot"
      refute_includes nano, "gcc-i386-musl"
      assert nano.none? { |n| n.start_with?("tilck-") }, "a stack is no member"
    end
  end

  def test_the_stack_installs_an_empty_tree_with_its_records
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("dflt", default: true))
        stack = register_tilck_stack!

        rc, _ = run_cli("-s", stack.name, "-q")
        assert_equal 0, rc

        inst = stack.get_install_list.find { |i| !i.path.nil? }
        refute_nil inst
        refute inst.broken
        assert inst.manual
        assert_equal %w[.build_inputs .built_against .install_origin],
                     inst.path.children.map { |c| c.basename.to_s }.sort
        assert_equal :auto,
                     pkgmgr.get("dflt").get_install_list.first.manual ? :manual
                                                                       : :auto
      end
    end
  end

  def test_a_stack_is_refused_at_another_board
    with_fake_tc do
      with_stubbed_externals do
        register_tilck_stack!
        other = TilckStackPackage.new(RV, "licheerv-nano")
        pkgmgr.register(other)
        with_context(ARCH: RV, BOARD: "qemu-virt") do
          rc, out = run_cli("-s", other.name, "-q")
          assert_equal 1, rc
          assert_match(/not supported/, out)
        end
      end
    end
  end

  def test_the_listing_opens_with_the_stacks
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("dflt", default: true))
        stack = register_tilck_stack!
        other = TilckStackPackage.new(RV, "qemu-virt")
        pkgmgr.register(other)
        run_cli("-q")

        out = StringIO.new
        old = $stdout
        $stdout = out
        pkgmgr.show_status_all
        $stdout = old
        plain = out.string.gsub(/\e\[[0-9;]*m/, "")

        assert_match(/\A\n---\s+Tilck stacks\s+---/, plain)
        assert_match(/^tilck-i386-pc\s+\[ built\s+\]\s+1 pkgs\s+\[ CURRENT \]/,
                     plain)
        assert_match(/^tilck-riscv64-qemu-virt\s+\[ not built \]\s+0 pkgs$/,
                     plain)
        refute_match(/^tilck-i386-pc\s+\[ installed/, plain,
                     "a stack is not a line in a section")
      end
    end
  end
end
