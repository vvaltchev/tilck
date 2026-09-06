# SPDX-License-Identifier: BSD-2-Clause
#
# THE MODEL, VALIDATED.
#
# A model that is wrong is the broken instrument: everything diffed
# against it looks right, and is not. So before the exhaustive lane
# trusts tests/model/model.rb, this file checks it two ways.
#
# First, against history. Every logic bug the package manager has had
# is a case here, with the answer written by hand from what the bug
# report said was RIGHT -- not from what the code does now. If the
# model cannot get these, it is describing the bugs rather than the
# contract.
#
# Second, on its own terms. select() is total (it never names an
# installation the world does not have), the transitions are
# deterministic, and a dry run changes nothing -- laws the model must
# obey before it can be asked to judge anything else.
#

require_relative 'test_helper'
require_relative 'model/model'

class TestModel < Minitest::Test

  include TestHelper

  I386 = ALL_ARCHS["i386"]
  X64  = ALL_ARCHS["x86_64"]
  RV   = ALL_ARCHS["riscv64"]

  GCC  = Ver("13.3.0")
  A    = Ver("14.4.0")
  B    = Ver("16.2.0")

  # Coordinates the cases talk about.
  def tgt(arch, board = nil)
    Coords.new("tilck-#{arch.name}", board || arch.default_board, "gcc-#{GCC}")
  end

  def stack(v) = Coords.new(HOST_OS_ARCH, nil, "gcc-#{v}")
  def distro   = Coords.new(HOST_OS_ARCH, HOST_DISTRO, nil)

  def inv(arch: I386, board: nil, stack: A, os: "linux", host: "x86_64")
    Model::Inv.new(env_arch: arch, env_board: board, default_stack: stack,
                   host_os: os, host_arch: host)
  end

  def reg(*shapes) = Model::Registry.new(shapes)

  def go(registry, world, argv, inv)
    Model.step(registry, world, Model.parse(argv.split), inv)
  end

  def k(*a, **kw) = Model.key(*a, **kw)

  # Every arch has a compiler version in the fake world.
  def setup
    @saved = ALL_ARCHS.transform_values(&:gcc_ver)
    ALL_ARCHS.each_value { |a| a.gcc_ver = GCC }
  end

  def teardown
    @saved.each { |n, v| ALL_ARCHS[n].gcc_ver = v }
  end

  # --- the history ---------------------------------------------------------

  # `-H 14.4.0 -u host_qemu:6.2.0` removed nothing: the filter used
  # `default_cc == "syscc"` as a proxy for "host package", which is
  # false for a :stack one.
  def test_uninstall_in_one_stack_leaves_the_other
    r = reg(Model::Shape.make("host_qemu", :stack, versions: %w[6.2.0]))
    w = Model.world(k("host_qemu", "6.2.0", stack(A)),
                    k("host_qemu", "6.2.0", stack(B)))

    o = go(r, w, "-H 14.4.0 -u host_qemu:6.2.0", inv)
    assert_equal 0, o.rc
    assert_equal Model.world(k("host_qemu", "6.2.0", stack(B))), o.world
  end

  # `-f -s host_qemu:6.2.0` removed 6.2.0 AND 11.1.0: nil passed for
  # the compiler, which the filter read as "any".
  def test_force_rebuilds_one_version_and_keeps_the_other
    r = reg(Model::Shape.make("host_qemu", :stack, versions: %w[6.2.0 11.1.0]))
    w = Model.world(k("host_qemu", "6.2.0", stack(A), record: :changed),
                    k("host_qemu", "11.1.0", stack(A)))

    o = go(r, w, "-f -s host_qemu:6.2.0", inv)
    assert_equal 0, o.rc
    assert_equal Model.world(k("host_qemu", "6.2.0", stack(A), origin: :pinned),
                             k("host_qemu", "11.1.0", stack(A))),
                 o.world
  end

  # `-u zlib` on riscv64 removed qemu-virt AND licheerv-nano: it
  # matched on the arch, which is two thirds of a coordinate.
  def test_uninstall_takes_one_board_only
    r = reg(Model::Shape.make("zlib", :target, arch_list: %w[riscv64]))
    w = Model.world(k("zlib", "1.0.0", tgt(RV, "qemu-virt")),
                    k("zlib", "1.0.0", tgt(RV, "licheerv-nano")))

    o = go(r, w, "-u zlib", inv(arch: RV, board: "qemu-virt"))
    assert_equal Model.world(k("zlib", "1.0.0", tgt(RV, "licheerv-nano"))),
                 o.world
  end

  # --check-for-updates called 22 fresh packages stale: their recipe
  # was rendered at the CURRENT stack instead of at each install's.
  # In the model a record is a property of the key, so the scope's
  # stack cannot reach it -- the case pins that it does not.
  def test_staleness_is_a_property_of_the_install
    r = reg(Model::Shape.make("host_x11", :stack))
    w = Model.world(k("host_x11", "1.0.0", stack(A)),
                    k("host_x11", "1.0.0", stack(B), record: :changed))

    from_a = go(r, w, "--check-for-updates", inv(stack: A))
    from_b = go(r, w, "--check-for-updates", inv(stack: B))

    assert_equal 2, from_a.rc
    assert_equal from_a.out, from_b.out
    assert_equal "NEEDS_REBUILD host_x11", from_a.out

    fresh = Model.world(k("host_x11", "1.0.0", stack(A)),
                        k("host_x11", "1.0.0", stack(B)))
    assert_equal 0, go(r, fresh, "--check-for-updates", inv(stack: B)).rc
  end

  # `-a riscv64 -f -s uboot` from an i386 shell: "requires board
  # qemu-virt" -- board_supported? read the shell's BOARD -- after -f
  # had already removed the install.
  def test_a_board_package_is_rebuilt_through_the_arch_flag
    r = reg(Model::Shape.make("uboot", :target, arch_list: %w[riscv64],
                              board_list: %w[qemu-virt]))
    w = Model.world(k("uboot", "1.0.0", tgt(RV, "qemu-virt"),
                      record: :changed))

    o = go(r, w, "-a riscv64 -f -s uboot", inv(arch: I386, board: "pc"))
    assert_equal 0, o.rc, o.out
    assert_equal Model.world(k("uboot", "1.0.0", tgt(RV, "qemu-virt"))),
                 o.world
  end

  # ...and where the board really is unsupported, nothing is removed
  # first. SPEC: support is checked before -f touches the tree.
  def test_an_unsupported_board_removes_nothing
    r = reg(Model::Shape.make("uboot", :target, arch_list: %w[riscv64],
                              board_list: %w[qemu-virt]))
    w = Model.world(k("uboot", "1.0.0", tgt(RV, "qemu-virt")))

    o = go(r, w, "-f -s uboot", inv(arch: RV, board: "licheerv-nano"))
    assert_equal 1, o.rc
    assert_equal w, o.world
  end

  # The install for one board must not answer "already installed"
  # for the other.
  def test_the_other_board_does_not_count_as_installed
    r = reg(Model::Shape.make("boardy", :target, arch_list: %w[riscv64]))
    w = Model.world(k("boardy", "1.0.0", tgt(RV, "qemu-virt")))

    o = go(r, w, "-s boardy", inv(arch: RV, board: "licheerv-nano"))
    assert_equal 0, o.rc
    assert_equal Model.world(k("boardy", "1.0.0", tgt(RV, "qemu-virt")),
                             k("boardy", "1.0.0", tgt(RV, "licheerv-nano"))),
                 o.world
  end

  # `-s host_stacky` under -H B, installed only at A: builds at B.
  def test_a_stack_package_installs_into_the_named_stack
    r = reg(Model::Shape.make("host_stacky", :stack))
    w = Model.world(k("host_stacky", "1.0.0", stack(A)))

    o = go(r, w, "-H 16.2.0 -s host_stacky", inv(stack: A))
    assert_equal Model.world(k("host_stacky", "1.0.0", stack(A)),
                             k("host_stacky", "1.0.0", stack(B))),
                 o.world
  end

  # -s ALL on i386 skips a riscv64-only package and takes the rest.
  def test_install_all_skips_what_this_arch_cannot_build
    r = reg(Model::Shape.make("universal", :target,
                              arch_list: %w[i386 x86_64 riscv64]),
            Model::Shape.make("rv_only", :target, arch_list: %w[riscv64]),
            Model::Shape.make("gcc-i386-musl", :cross_cc, target_arch: "i386"))
    o = go(r, Model.world, "-s universal -s rv_only -a ALL", inv)

    assert_equal 0, o.rc
    names = o.world.map { |x| [x.name, x.coords.machine] }.to_set
    assert_includes names, ["universal", "tilck-i386"]
    assert_includes names, ["universal", "tilck-riscv64"]
    assert_includes names, ["rv_only", "tilck-riscv64"]
    refute_includes names, ["rv_only", "tilck-i386"]
  end

  # --upgrade installs the new default beside an old DEFAULT install
  # and leaves a pinned one alone.
  def test_upgrade_moves_defaults_and_leaves_pins
    r = reg(Model::Shape.make("multi", :target, versions: %w[2.0.0 1.0.0],
                              arch_list: %w[i386]))
    old_default = Model.world(k("multi", "1.0.0", tgt(I386)))
    pinned = Model.world(k("multi", "1.0.0", tgt(I386), origin: :pinned))

    o = go(r, old_default, "--upgrade", inv)
    assert_equal Model.world(k("multi", "1.0.0", tgt(I386)),
                             k("multi", "2.0.0", tgt(I386))), o.world

    assert_equal pinned, go(r, pinned, "--upgrade", inv).world
  end

  # A version conflict installs nothing -- and with -f, removes nothing.
  # SPEC: the conflict is found before the tree is touched.
  def test_a_conflict_touches_nothing_even_with_force
    r = reg(Model::Shape.make("host_a", :distro,
                              deps: [["host_shared", "1.0.0"]]),
            Model::Shape.make("host_b", :distro,
                              deps: [["host_shared", "2.0.0"]]),
            Model::Shape.make("host_shared", :distro,
                              versions: %w[1.0.0 2.0.0]))
    w = Model.world(k("host_a", "1.0.0", distro))

    o = go(r, w, "-f -s host_a -s host_b", inv)
    assert_equal 1, o.rc
    assert_equal w, o.world
  end

  # A dependency already installed is not walked: `-s a` with a
  # present and its dep absent installs nothing.
  def test_an_installed_node_cuts_the_closure
    r = reg(Model::Shape.make("host_a", :distro, deps: [["host_b", nil]]),
            Model::Shape.make("host_b", :distro))
    w = Model.world(k("host_a", "1.0.0", distro))

    o = go(r, w, "-s host_a", inv)
    assert_equal w, o.world
    assert_equal "already installed", o.out
  end

  # A pin reaches a dependency: -s host_a installs host_shared at the
  # pinned version, and records it as pinned.
  def test_a_pin_reaches_the_dependency
    r = reg(Model::Shape.make("host_a", :distro,
                              deps: [["host_shared", "2.0.0"]]),
            Model::Shape.make("host_shared", :distro,
                              versions: %w[1.0.0 2.0.0]))

    o = go(r, Model.world, "-s host_a", inv)
    assert_equal Model.world(k("host_a", "1.0.0", distro),
                             k("host_shared", "2.0.0", distro,
                               origin: :pinned)),
                 o.world
  end

  # `-u ALL` keeps the cross compilers unless -f, and never takes ruby.
  def test_uninstall_all_keeps_compilers_and_ruby
    r = reg(Model::Shape.make("zlib", :target, arch_list: %w[i386]),
            Model::Shape.make("gcc-i386-musl", :cross_cc, target_arch: "i386"),
            Model::Shape.make("ruby", :distro))
    cc = Coords.new(HOST_OS_ARCH, nil, nil)
    w = Model.world(k("zlib", "1.0.0", tgt(I386)),
                    k("gcc-i386-musl", "13.3.0", cc),
                    k("ruby", "3.4.7", distro))

    plain = go(r, w, "-u ALL", inv)
    assert_equal Model.world(k("gcc-i386-musl", "13.3.0", cc),
                             k("ruby", "3.4.7", distro)), plain.world

    forced = go(r, w, "-u ALL -f", inv)
    assert_equal Model.world(k("ruby", "3.4.7", distro)), forced.world
  end

  # An orphan -- on disk, no package -- goes everywhere it is, unless
  # -a names the arch.
  def test_an_orphan_is_removed_wherever_it_is
    r = reg
    w = Model.world(k("gone", "1.0.0", tgt(I386)), k("gone", "1.0.0", tgt(RV)))
    assert_equal Model.world, go(r, w, "-u gone", inv).world
    assert_equal Model.world(k("gone", "1.0.0", tgt(I386))),
                 go(r, w, "-u gone -a riscv64", inv).world
  end

  # --- the model's own laws ------------------------------------------------

  # select never names what the world does not have.
  def test_select_is_total
    r = reg(Model::Shape.make("zlib", :target, arch_list: %w[i386 riscv64]),
            Model::Shape.make("host_s", :stack))
    w = Model.world(k("zlib", "1.0.0", tgt(I386)),
                    k("zlib", "1.0.0", tgt(RV, "licheerv-nano")),
                    k("host_s", "1.0.0", stack(A)))
    sc = Model.scope(inv, Model.parse([]), arch_is_scope: false)

    for argv in ["-u zlib", "-u zlib -a riscv64", "-u zlib -a ALL",
                 "-u zlib:9.9.9", "-u host_s", "-u host_s -c 16.2.0",
                 "-u ALL", "-u ALL -f", "-u ALL -c 13.3.0", "-u nothing"] do
      picked = Model.select(r, w, Model.parse(argv.split), sc)
      assert picked.subset?(w), "#{argv}: named #{(picked - w).to_a}"
    end
  end

  # Every destructive mode with -d leaves the world as it was.
  def test_dry_run_changes_nothing
    r = reg(Model::Shape.make("zlib", :target, versions: %w[2.0.0 1.0.0],
                              arch_list: %w[i386]),
            Model::Shape.make("host_s", :stack))
    w = Model.world(k("zlib", "1.0.0", tgt(I386)),
                    k("host_s", "1.0.0", stack(A), record: :changed))

    for argv in ["-s zlib -d", "-s zlib -f -d", "-s host_s -d", "-u zlib -d",
                 "-u ALL -d", "-u ALL -f -d", "--upgrade -d", "--clean -d",
                 "-C zlib -d", "-d"] do
      assert_equal w, go(r, w, argv, inv).world, argv
    end
  end

  def test_deterministic
    r = reg(Model::Shape.make("host_a", :distro, deps: [["host_b", nil]]),
            Model::Shape.make("host_b", :distro))
    a = go(r, Model.world, "-s host_a", inv)
    b = go(r, Model.world, "-s host_a", inv)
    assert_equal a, b
  end

  # The parser reads the grammar the exhaustive lane will emit.
  def test_parse
    q = Model.parse(%w[-s foo:1.2.0 -s bar -f -d -a riscv64 -H 16.2.0])
    assert_equal :install, q.mode
    assert_equal [["foo", Ver("1.2.0")], ["bar", nil]], q.targets
    assert q.force && q.dry
    assert_equal RV, q.arch
    assert_equal Ver("16.2.0"), q.stack

    assert_equal [[:all, nil]], Model.parse(%w[-u ALL]).targets
    assert_equal [["x", :all]], Model.parse(%w[-u x:ALL]).targets
    assert_equal :all, Model.parse(%w[-s x -a ALL]).arch
    assert_equal [["gcc-riscv64-musl", nil]],
                 Model.parse(%w[-S riscv64]).targets
    assert_equal :default, Model.parse([]).mode
  end
end

# A version is resolved against what the package declares, or refused
# at the door -- so that `host_qemu:6` means 6.2.0 rather than a
# two-hour build of the compiler for a tarball that does not exist.
class TestModelResolvesVersions < Minitest::Test

  include TestHelper

  def reg(*shapes) = Model::Registry.new(shapes)
  def ctx = Model::Inv.new(env_arch: ALL_ARCHS["i386"], env_board: "pc",
                           default_stack: Ver("14.4.0"),
                           host_os: "linux", host_arch: "x86_64")

  def install(r, argv)
    Model.step(r, Set.new, Model.parse(argv), ctx)
  end

  def test_a_series_names_the_one_release_it_has
    r = reg(Model::Shape.make("q", :stack, versions: %w[6.2.0 7.2.0]))
    o = install(r, %w[-s q:6])
    assert_equal 0, o.rc, o.inspect
    assert_equal [Ver("6.2.0")], o.world.map(&:ver)
  end

  def test_a_version_nobody_offers_is_refused_and_nothing_changes
    r = reg(Model::Shape.make("q", :stack, versions: %w[6.2.0 7.2.0]))
    o = install(r, %w[-s q:9.9.9])
    assert_equal 1, o.rc
    assert_empty o.world
  end

  def test_a_series_with_two_releases_is_refused
    r = reg(Model::Shape.make("q", :stack, versions: %w[6.1.0 6.2.0]))
    assert_equal 1, install(r, %w[-s q:6]).rc
  end

  def test_a_package_declaring_nothing_takes_the_version_as_written
    r = reg(Model::Shape.make("t", :target, arch_list: %w[i386]))
    o = install(r, %w[-s t:3.3.3])
    assert_equal 0, o.rc, o.inspect
    assert_equal [Ver("3.3.3")], o.world.map(&:ver)
  end
end

# --rebuild in the model: a record that does not read :ok is made to,
# in place; a bumped version is --upgrade's; -d moves nothing.
class TestModelRebuild < Minitest::Test

  include TestHelper

  def reg(*shapes) = Model::Registry.new(shapes)
  def ctx = Model::Inv.new(env_arch: ALL_ARCHS["i386"], env_board: "pc",
                           default_stack: Ver("14.4.0"),
                           host_os: "linux", host_arch: "x86_64")
  def tgt = Coords.new("tilck-i386", "pc", "gcc-#{ALL_ARCHS["i386"].gcc_ver}")

  def test_a_changed_record_is_rebuilt_in_place_as_it_was_asked_for
    r = reg(Model::Shape.make("t", :target, arch_list: %w[i386]))
    k = Model.key("t", "1.0.0", tgt, record: :changed, origin: :pinned)
    o = Model.step(r, Set[k], Model.parse(%w[--rebuild]), ctx)
    assert_equal 0, o.rc
    assert_equal [Model.key("t", "1.0.0", tgt, origin: :pinned)], o.world.to_a
  end

  def test_dry_run_moves_nothing
    r = reg(Model::Shape.make("t", :target, arch_list: %w[i386]))
    k = Model.key("t", "1.0.0", tgt, record: :missing)
    o = Model.step(r, Set[k], Model.parse(%w[--rebuild -d]), ctx)
    assert_equal Set[k], o.world
  end

  def test_a_bumped_version_is_not_a_rebuild
    r = reg(Model::Shape.make("t", :target, arch_list: %w[i386],
                              versions: %w[2.0.0], default_ver: "2.0.0"))
    k = Model.key("t", "1.0.0", tgt, record: :changed, origin: :default)
    o = Model.step(r, Set[k], Model.parse(%w[--rebuild]), ctx)
    assert_equal Set[k], o.world, "an upgrade candidate was rebuilt in place"
  end
end

# A gcc installed as the default of its own stack is not an upgrade
# candidate from another stack's scope: it is that stack.
class TestModelStackCompilerNeverUpgrades < Minitest::Test

  include TestHelper

  def test_a_default_gcc_of_another_stack_is_left_alone
    r = Model::Registry.new([Model::Shape.make("host_gcc", :stack_cc,
                                               versions: %w[12.5.0 14.4.0],
                                               default_ver: "14.4.0")])
    inv = Model::Inv.new(env_arch: ALL_ARCHS["i386"], env_board: "pc",
                         default_stack: Ver("14.4.0"),
                         host_os: "linux", host_arch: "x86_64")
    c = Coords.new(HOST_OS_ARCH, HOST_DISTRO, nil)
    k = Model.key("host_gcc", "12.5.0", c, origin: :default)

    o = Model.step(r, Set[k], Model.parse(%w[--check-for-updates]), inv)
    assert_equal 0, o.rc, o.out
    o = Model.step(r, Set[k], Model.parse(%w[--upgrade]), inv)
    assert_equal Set[k], o.world
  end
end

# A rebuild puts a dependency the recipe has grown in first.
class TestModelRebuildPlansItsDependencies < Minitest::Test

  include TestHelper

  def ctx = Model::Inv.new(env_arch: ALL_ARCHS["i386"], env_board: "pc",
                           default_stack: Ver("14.4.0"),
                           host_os: "linux", host_arch: "x86_64")
  def tgt = Coords.new("tilck-i386", "pc", "gcc-#{ALL_ARCHS["i386"].gcc_ver}")

  def test_a_missing_dependency_is_installed_and_the_install_rebuilt
    r = Model::Registry.new([
      Model::Shape.make("top", :target, arch_list: %w[i386],
                        deps: [["base", nil]]),
      Model::Shape.make("base", :target, arch_list: %w[i386]),
    ])
    k = Model.key("top", "1.0.0", tgt, record: :changed)
    o = Model.step(r, Set[k], Model.parse(%w[--rebuild]), ctx)
    assert_equal 0, o.rc, o.out
    assert_equal [["base", Ver("1.0.0")], ["top", Ver("1.0.0")]],
                 o.world.map { |x| [x.name, x.ver] }.sort
    assert o.world.all? { |x| x.record == :ok }
  end
end
