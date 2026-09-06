# SPDX-License-Identifier: BSD-2-Clause
#
# THE LAWS, SHOWN TO SEE.
#
# A check that runs after every command line in the suite is worth
# exactly as much as its ability to fail. So each law is handed a
# violation built by hand and must report it; and the harness's
# opt-out must refuse to be used silently.
#

require_relative 'test_helper'

class TestLaws < Minitest::Test

  include TestHelper

  I386 = ALL_ARCHS["i386"]

  def tgt = Coords.new("tilck-i386", "pc", "gcc-#{FAKE_GCC_VER}")

  def snap(world, shapes)
    Bridge::Snapshot.new(
      registry: Model::Registry.new(shapes),
      world: world,
      inv: Model::Inv.new(env_arch: I386, env_board: "pc",
                          default_stack: Ver("14.4.0"),
                          host_os: "linux", host_arch: "x86_64"),
      misplaced: [],
    )
  end

  def foo = Model::Shape.make("foo", :target, arch_list: %w[i386])

  def setup
    @saved = ALL_ARCHS.transform_values(&:gcc_ver)
    ALL_ARCHS.each_value { |a| a.gcc_ver = FAKE_GCC_VER }
  end

  def teardown
    @saved.each { |n, v| ALL_ARCHS[n].gcc_ver = v }
  end

  # L1: the implementation removed the other one.
  def test_l1_sees_an_effect_the_model_did_not_make
    other = Coords.new("tilck-riscv64", "qemu-virt", "gcc-#{FAKE_GCC_VER}")
    before = Model.world(Model.key("foo", "1.0.0", tgt),
                         Model.key("foo", "1.0.0", other))
    after = Model.world(Model.key("foo", "1.0.0", other))   # -u took one
    wrong = Model.world                                     # ...took both

    ok = Laws.check(%w[-u foo], snap(before, [foo]), snap(after, [foo]))
    assert_empty ok.map(&:to_s)

    broken = Laws.check(%w[-u foo], snap(before, [foo]), snap(wrong, [foo]))
    assert_equal [:L1_model], broken.map(&:law)
    assert_match(/only in model:\n\s+foo@1.0.0 tilck-riscv64/,
                 broken.first.to_s)
  end

  # L2: -d changed the world, whatever the model thinks.
  def test_l2_sees_a_dry_run_that_touched_the_tree
    before = Model.world(Model.key("foo", "1.0.0", tgt))
    broken = Laws.check(%w[-u foo -d], snap(before, [foo]),
                        snap(Model.world, [foo]))
    assert_includes broken.map(&:law), :L2_dry_run
  end

  # L4: something got installed without a record that reads ok.
  def test_l4_sees_an_install_without_a_record
    after = Model.world(Model.key("foo", "1.0.0", tgt, record: :missing))
    broken = Laws.check(%w[-s foo], snap(Model.world, [foo]),
                        snap(after, [foo]))
    assert_includes broken.map(&:law), :L4_recorded
  end

  # A line outside the grammar is skipped and counted, never passed
  # off as checked.
  def test_an_unparsed_line_is_counted
    n = Laws.unparsed.length
    out = Laws.check(%w[--deps foo], snap(Model.world, [foo]),
                     snap(Model.world, [foo]))
    assert_empty out
    assert_equal n + 1, Laws.unparsed.length
    assert_equal %w[--deps foo], Laws.unparsed.last
  end

  # The opt-out needs a reason.
  def test_switching_the_laws_off_needs_a_reason
    assert_raises(ArgumentError) { run_cli("-h", laws: false) }
  end

  # ...and the laws really run around a real command line: a world the
  # implementation and the model agree on passes, end to end.
  def test_the_laws_run_around_a_command_line
    with_fake_tc do
      with_stubbed_externals do
        reset_pkgmgr!
        pkgmgr.register(FakePackage.new("foo"))
        rc, _ = run_cli("-s", "foo", "-q")
        assert_equal 0, rc
        assert pkgmgr.get("foo").installed?(Ver("1.0.0"))
      end
    end
  end
end
