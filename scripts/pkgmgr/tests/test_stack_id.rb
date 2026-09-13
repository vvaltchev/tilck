# SPDX-License-Identifier: BSD-2-Clause
#
# THE STACK COORDINATE AS A VALUE, and the schema's readers using it.
#
# The schema has promised since toolchain5 that gcc-14.4.0-lto or a
# clang stack is legal without a schema change. Every reader of a
# stack directory did a sub("gcc-", "") of its own instead, so an
# install in such a stack was an orphan to the scan, absent from -L,
# and invisible to its own package. The first half of this file is
# the value; the second builds fake stacks on disk and holds the
# readers to it.
#

require_relative 'test_helper'
require_relative '../stack_id'
require_relative '../world'

class TestStackIdValue < Minitest::Test

  def test_the_plain_stack_of_a_version
    id = StackId.of(Ver("14.4.0"))
    assert_equal "gcc-14.4.0", id.to_s
    assert id.plain?
    assert_equal id, StackId.parse("gcc-14.4.0")
    assert_equal id, StackId.coerce(Ver("14.4.0"))
    assert_same id, StackId.coerce(id)
  end

  def test_a_variant_and_a_foreign_family_round_trip
    for s in %w[gcc-14.4.0-lto gcc-13.3.0-musl clang-18.1.0
                gcc-14.4.0-musl-lto zig-0.13.0-baseline.v2] do
      id = StackId.parse(s)
      refute_nil id, s
      assert_equal s, id.to_s, "does not round trip"
      refute id.plain?, "#{s} is not the plain gcc stack"
    end
    lto = StackId.parse("gcc-14.4.0-lto")
    assert_equal ["gcc", Ver("14.4.0"), "lto"],
                 [lto.family, lto.ver, lto.variant]
    assert_equal "musl-lto", StackId.parse("gcc-14.4.0-musl-lto").variant
  end

  # The version is the compiler's, whatever surrounds it.
  def test_the_version_is_read_through_the_family_and_the_variant
    assert_equal Ver("18.1.0"), StackId.parse("clang-18.1.0").ver
    assert_equal Ver("14.4.0"), StackId.parse("gcc-14.4.0-lto").ver
  end

  def test_what_is_not_a_stack_does_not_parse
    for s in ["any", "", "gcc-", "14.4.0", "gcc-abc", "GCC-14.4.0",
              "gcc-14.4.0-", "gcc-14.4.0-Lto", "some-other",
              "gcc-riscv64-musl", " gcc-14.4.0", "gcc_14.4.0",
              "gcc-14.4.0/lto"] do
      assert_nil StackId.parse(s), "#{s.inspect} parsed as a stack"
    end
  end

  def test_coerce_refuses_what_is_neither
    assert_raises(ArgumentError) { StackId.coerce("gcc-14.4.0") }
    assert_raises(ArgumentError) { StackId.coerce(nil) }
  end

  # Ordered by family, then version, then variant: a listing sorts
  # the same whatever is on disk, and the plain stack of a version
  # comes before its variants.
  def test_ordering
    ids = %w[gcc-14.4.0-lto clang-18.1.0 gcc-9.2.0 gcc-14.4.0 gcc-11.5.0]
            .map { |s| StackId.parse(s) }
    assert_equal %w[clang-18.1.0 gcc-9.2.0 gcc-11.5.0 gcc-14.4.0
                    gcc-14.4.0-lto], ids.sort.map(&:to_s)
    refute_equal StackId.parse("gcc-14.4.0"), StackId.parse("gcc-14.4.0-lto")
    assert_equal StackId.parse("gcc-14.4.0").hash,
                 StackId.of(Ver("14.4.0")).hash
  end

  # Not comparable with anything else: a spelling is not a stack.
  def test_only_a_stack_compares_with_a_stack
    id = StackId.parse("gcc-14.4.0")
    assert_nil id <=> "gcc-14.4.0"
    assert_nil id <=> Ver("14.4.0")
    refute_equal id, "gcc-14.4.0"
  end
end

# Fake stacks on disk: a variant of the current compiler, and a
# foreign compiler's, each with an install in it.
class TestStackIdOnDisk < Minitest::Test

  include TestHelper

  CLANG = StackId.parse("clang-18.1.0")

  def setup
    reset_pkgmgr!
  end

  # The plain stack the fake world builds into, and a variant of it.
  def plain = StackId.of(pkgmgr.default_stack_cc_ver)
  def lto   = StackId.of(plain.ver, variant: "lto")

  def stack_coords(id) = Coords.new(HOST_OS_ARCH, nil, id.to_s)

  # A :stack package installed in three stacks: the plain one, the
  # variant, the foreign one.
  def install_in_three_stacks
    pkg = FakePackage.new("host_thing", on_host: true, host_tier: :stack,
                          arch_list: ALL_HOST_ARCHS.values)
    pkgmgr.register(pkg)
    fake_install(pkg)
    fake_install(pkg, at: stack_coords(lto))
    fake_install(pkg, at: stack_coords(CLANG))
    pkgmgr.refresh
    return pkg
  end

  def test_every_stack_on_disk_is_listed_as_it_is_spelled
    with_fake_tc do
      install_in_three_stacks
      assert_equal ["clang-18.1.0", plain.to_s, lto.to_s],
                   pkgmgr.host_stacks.map(&:to_s)
    end
  end

  # The package sees all three installs, each at its own coordinates,
  # with the compiler version read through the spelling.
  def test_a_stack_package_sees_its_installs_in_every_stack
    with_fake_tc do
      pkg = install_in_three_stacks
      installs = pkg.get_install_list.reject { |i| i.path.nil? }
      assert_equal [stack_coords(CLANG), stack_coords(plain),
                    stack_coords(lto)].map(&:to_s),
                   installs.map { |i| i.coords.to_s }.sort
      assert_equal [plain.ver, plain.ver, Ver("18.1.0")],
                   installs.map(&:compiler).sort
    end
  end

  # ...and the scan claims them, so none is an orphan.
  def test_installs_in_a_variant_stack_are_not_orphans
    with_fake_tc do
      install_in_three_stacks
      w = World.scan(pkgmgr.all_packages)
      assert_empty w.orphans, w.orphans.map(&:to_s)
      assert_equal 3, w.of("host_thing").length
    end
  end

  # A target package built into a variant stack of its cross compiler
  # is found, with the compiler version read through the variant.
  def test_a_target_install_in_a_variant_stack_is_found
    with_fake_tc do
      pkg = FakePackage.new("zlib")
      pkgmgr.register(pkg)
      at = Coords.new("tilck-#{ARCH.name}", ARCH.default_board,
                      "gcc-#{ARCH.gcc_ver}-lto")
      fake_install(pkg, at: at)
      pkgmgr.refresh

      installs = pkg.get_install_list.reject { |i| i.path.nil? }
      assert_equal [at.to_s], installs.map { |i| i.coords.to_s }
      assert_equal [ARCH.gcc_ver], installs.map(&:compiler)
      assert_empty World.scan(pkgmgr.all_packages).orphans
    end
  end

  # What is not spelled like a stack is not scanned as one.
  def test_a_directory_that_is_not_a_stack_is_left_alone
    with_fake_tc do
      pkg = FakePackage.new("host_thing", on_host: true, host_tier: :stack,
                            arch_list: ALL_HOST_ARCHS.values)
      pkgmgr.register(pkg)
      FileUtils.mkdir_p(Coords.new(HOST_OS_ARCH, nil, "some-other").pkgs_dir /
                        "thing" / "1.0.0")
      pkgmgr.refresh
      assert_empty pkgmgr.host_stacks
      assert_empty pkg.get_install_list.reject { |i| i.path.nil? }
    end
  end

  # -L lists every stack; -H refuses to build into one that is not
  # plain gcc, saying so rather than "unknown".
  def test_the_listing_shows_variant_stacks_and_H_cannot_select_them
    with_fake_tc do
      with_stubbed_externals do
        install_in_three_stacks
        gcc = FakePackage.new("host_gcc", on_host: true, host_tier: :distro,
                              arch_list: ALL_HOST_ARCHS.values)
        v = plain.ver
        gcc.define_singleton_method(:installable_versions) { [v] }
        pkgmgr.register(gcc)

        rc, out = run_cli("-L", "-q")
        assert_equal 0, rc, out
        assert_match(/#{lto}/, out)
        assert_match(/clang-18.1.0/, out)

        rc, out = run_cli("-H", lto.to_s, "-L", "-q")
        assert_equal 1, rc
        assert_match(/Cannot build into #{lto}/, out)
        refute_match(/Unknown host GCC stack/, out)
      end
    end
  end

  # Recomposing after a removal walks every stack; a stack the tool
  # cannot build into is said and left as it is, not emptied.
  def test_recomposition_leaves_a_variant_stack_as_it_is
    with_fake_tc do
      with_stubbed_externals do
        install_in_three_stacks
        marker = stack_coords(lto).sysroot / "usr" / "lib" / "keep"
        FileUtils.mkdir_p(marker.dirname)
        File.write(marker, "")

        out = capture_io { pkgmgr.compose_stack_sysroot(lto) }.join
        assert_match(/Not composing the sysroot of #{lto}/, out)
        assert marker.exist?, "the variant stack's sysroot was touched"
      end
    end
  end
end
