# SPDX-License-Identifier: BSD-2-Clause
#
# THE ARGUMENT LAYER OF -u, AS A TABLE.
#
# Every -u bug lived in the code that turned (name, version, compiler,
# arch) into a set of installations: force_remove passing nil where it
# meant "any", the "syscc" proxy for "host package", the arch that
# matched both boards. That code is now one function,
# PackageManager#uninstall_selector, and this is its table: each row a
# package of one kind, the arguments as the CLI hands them over, and
# the coordinates the selector must name -- written out, so that a
# reviewer checks each against the layout in docs/package_manager.md
# without running anything.
#
# The selector is a value, so it is tested as one: what does it match,
# not what happened afterwards. test_cli_matrix.rb covers the tree.
#

require_relative 'test_helper'

class TestInstallSelector < Minitest::Test

  include TestHelper

  RV = ALL_ARCHS["riscv64"]
  I386 = ALL_ARCHS["i386"]
  V1 = Ver("1.0.0")
  V2 = Ver("2.0.0")

  def setup
    reset_pkgmgr!
  end

  # An install list with one entry per coordinates given, at `ver`.
  def installs(pkg, ver, *coords)
    coords.map { |c|
      InstallInfo.new(pkg&.name || "gone", nil, false, nil, ver,
                      c.pkgs_dir / "x" / ver.to_s, pkg, false, coords: c)
    }
  end

  def sel(pkg, list, **kw)
    pkgmgr.uninstall_selector(pkg, pkg&.name || "gone", list, **kw)
  end

  def matched(s, list) = list.select { |i| s.matches?(i) }.map(&:coords)

  def tgt(arch, board = arch.default_board)
    Coords.new("tilck-#{arch.name}", board, "gcc-#{FAKE_GCC_VER}")
  end

  def stack(v) = Coords.new(HOST_OS_ARCH, nil, "gcc-#{v}")

  # --- the filter value itself ----------------------------------------------

  def test_a_filter_matches_level_by_level
    c = Coords.new("tilck-i386", "pc", "gcc-13.3.0")
    assert CoordsFilter.any.include?(c)
    assert CoordsFilter.exact(c).include?(c)
    assert CoordsFilter.new(machine: "tilck-i386", env: :any, stack: :any)
                       .include?(c)
    refute CoordsFilter.new(machine: "tilck-i386", env: "qemu-virt",
                            stack: :any).include?(c)
    refute CoordsFilter.any.include?(nil), "no coordinates match nothing"
  end

  def test_a_selector_needs_all_three
    c = Coords.new("tilck-i386", "pc", "gcc-13.3.0")
    i = InstallInfo.new("zlib", nil, false, nil, V1, c.pkgs_dir, nil, false,
                        coords: c)
    s = InstallSelector.new(name: "zlib", ver: V1, where: [CoordsFilter.exact(c)])
    assert s.matches?(i)
    refute InstallSelector.new(name: "other", ver: V1,
                               where: [CoordsFilter.exact(c)]).matches?(i)
    refute InstallSelector.new(name: "zlib", ver: V2,
                               where: [CoordsFilter.exact(c)]).matches?(i)
    refute InstallSelector.new(name: "zlib", ver: V1, where: []).matches?(i)
  end

  # --- target packages ------------------------------------------------------

  def test_target_default_is_this_arch_at_this_board
    with_fake_tc do
      pkg = FakePackage.new("zlib", arch_list: [RV])
      pkgmgr.register(pkg)
      list = installs(pkg, V1, tgt(RV, "qemu-virt"), tgt(RV, "licheerv-nano"))

      with_context(ARCH: RV, BOARD: "licheerv-nano") do
        assert_equal [tgt(RV, "licheerv-nano")], matched(sel(pkg, list), list)
      end
    end
  end

  def test_target_arch_flag_names_that_arch_at_its_board
    with_fake_tc do
      pkg = FakePackage.new("zlib")
      pkgmgr.register(pkg)
      list = installs(pkg, V1, tgt(I386), tgt(RV, "qemu-virt"),
                      tgt(RV, "licheerv-nano"))

      assert_equal [tgt(RV, "qemu-virt")],
                   matched(sel(pkg, list, arch: "riscv64"), list)
      assert_equal list.map(&:coords), matched(sel(pkg, list, arch: "ALL"), list)
    end
  end

  def test_target_compiler_flag_selects_the_stack
    with_fake_tc do
      pkg = FakePackage.new("zlib")
      pkgmgr.register(pkg)
      other = Coords.new("tilck-i386", "pc", "gcc-12.0.0")
      list = installs(pkg, V1, tgt(I386), other)

      assert_equal [other], matched(sel(pkg, list, compiler: Ver("12.0.0")), list)
      assert_equal [tgt(I386), other],
                   matched(sel(pkg, list, compiler: "ALL"), list)
    end
  end

  # --- versions -------------------------------------------------------------

  def test_no_version_means_the_default_if_here_else_everything_here
    with_fake_tc do
      pkg = FakePackage.new("multi")
      pkgmgr.register(pkg)

      both = installs(pkg, V1, tgt(I386)) + installs(pkg, V2, tgt(I386))
      assert_equal V1, sel(pkg, both).ver, "the default is here: only it"

      only_v2 = installs(pkg, V2, tgt(I386))
      assert_equal :all, sel(pkg, only_v2).ver, "default absent: what is here"

      # The default being installed ELSEWHERE says nothing about here.
      elsewhere = installs(pkg, V1, tgt(RV)) + installs(pkg, V2, tgt(I386))
      assert_equal :all, sel(pkg, elsewhere).ver
    end
  end

  def test_a_named_version_that_is_not_here_selects_nothing
    with_fake_tc do
      pkg = FakePackage.new("multi")
      pkgmgr.register(pkg)
      list = installs(pkg, V1, tgt(I386))
      assert_nil sel(pkg, list, ver: V2)
      assert_equal V1, sel(pkg, list, ver: V1).ver
      assert_equal :all, sel(pkg, list, ver: "ALL").ver
    end
  end

  # --- host packages --------------------------------------------------------

  def test_host_stack_package_follows_the_stack_in_effect
    with_fake_tc do
      pkg = FakePackage.new("host_s", on_host: true, host_tier: :stack,
                            arch_list: ALL_HOST_ARCHS.values)
      pkgmgr.register(pkg)
      a, b = Ver("7.7.7"), Ver("8.8.8")
      list = installs(pkg, V1, stack(a), stack(b))

      pkgmgr.with_host_stack(a) {
        assert_equal [stack(a)], matched(sel(pkg, list), list)
        assert_equal [stack(b)],
                     matched(sel(pkg, list, compiler: b), list)
      }
    end
  end

  def test_host_package_ignores_an_arch
    with_fake_tc do
      pkg = FakePackage.new("host_d", on_host: true, host_tier: :distro,
                            arch_list: ALL_HOST_ARCHS.values)
      pkgmgr.register(pkg)
      list = installs(pkg, V1, pkg.coords)

      assert_equal [pkg.coords], matched(sel(pkg, list), list)
      assert_empty matched(sel(pkg, list, arch: "riscv64"), list)
      assert_empty matched(sel(pkg, list, compiler: Ver("9.9.9")), list),
                   "a non-stack package is not built by a stack"
    end
  end

  # --- ALL and orphans ------------------------------------------------------

  def test_all_is_this_scope_plus_every_host_and_noarch
    with_fake_tc do
      t = FakePackage.new("t")
      h = FakePackage.new("host_h", on_host: true, host_tier: :distro,
                          arch_list: ALL_HOST_ARCHS.values)
      n = FakePackage.new("n", arch_list: nil)
      [t, h, n].each { |p| pkgmgr.register(p) }

      list = installs(t, V1, tgt(I386), tgt(RV)) + installs(h, V1, h.coords) +
             installs(n, V1, n.coords)
      s = pkgmgr.uninstall_selector(nil, "ALL", list)

      assert_equal [tgt(I386), h.coords, n.coords], matched(s, list)
      assert_equal list.map(&:coords),
                   matched(pkgmgr.uninstall_selector(nil, "ALL", list,
                                                     arch: "ALL"), list)
    end
  end

  def test_an_orphan_goes_everywhere_unless_an_arch_is_named
    with_fake_tc do
      list = installs(nil, V1, tgt(I386), tgt(RV))
      assert_equal [tgt(I386), tgt(RV)], matched(sel(nil, list), list)
      assert_equal [tgt(RV)], matched(sel(nil, list, arch: "riscv64"), list)
    end
  end

  def test_explicit_coordinates_beat_everything
    with_fake_tc do
      pkg = FakePackage.new("zlib")
      pkgmgr.register(pkg)
      list = installs(pkg, V1, tgt(I386), tgt(RV))
      s = sel(pkg, list, arch: "ALL", compiler: "ALL", coords: [tgt(RV)])
      assert_equal [tgt(RV)], matched(s, list)
    end
  end

  def test_an_unknown_arch_is_refused
    with_fake_tc do
      pkg = FakePackage.new("zlib")
      pkgmgr.register(pkg)
      assert_raises(ArgumentError) { sel(pkg, [], arch: "sparc") }
    end
  end
end
