# SPDX-License-Identifier: BSD-2-Clause
#
# THE ONE RECORD SHAPE, and .install on it.
#
# Three files beside an install said what it was: .install_origin
# (two words, or one), .built_against (name-version lines) and
# .build_inputs (digests), two of them unversioned and none saying
# WHAT the install is, WHERE it was written or BY WHOM -- all of
# that was read off the path with today's rule. Now every record the
# package manager writes is `key: value` lines (record.rb), and
# .install carries the identity as written, the marks, the host, the
# stack and what the install was built against.
#

require_relative 'test_helper'
require_relative '../record'

class TestRecordShape < Minitest::Test

  def test_a_record_is_key_colon_value_lines
    kv = Record.parse("format: 1\nname: host_qemu\nhost: linux-x86_64 a b\n")
    assert_equal ["1"], kv["format"]
    assert_equal ["host_qemu"], kv["name"]
    assert_equal ["linux-x86_64 a b"], kv["host"], "spaces in a value"
    assert_equal 1, Record.format_of(kv)
  end

  def test_a_value_may_hold_colons_and_a_key_may_repeat
    kv = Record.parse("recipe: sha256:abc\nagainst: a 1\nagainst: b 2\n")
    assert_equal ["sha256:abc"], kv["recipe"]
    assert_equal ["a 1", "b 2"], kv["against"]
    assert_equal "sha256:abc", Record.one(kv, "recipe")
    assert_equal "a 1", Record.one(kv, "against")
  end

  def test_a_line_with_no_separator_is_a_broken_file
    assert_nil Record.parse("format: 1\nkind host\n")
    assert_nil Record.parse("format 1\n")
    assert_nil Record.parse("Format: 1\n"), "a key is lower case"
  end

  def test_blank_lines_are_nothing_and_unknown_keys_are_kept
    kv = Record.parse("format: 1\n\nwhatever: x\n")
    assert_equal ["x"], kv["whatever"]
    assert_equal [], kv["missing"]
    assert_nil Record.one(kv, "missing")
  end

  def test_render_and_parse_round_trip
    pairs = [["format", 1], ["name", "t"], ["against", ["a 1", "b 2"]]]
    text = Record.render(pairs)
    assert_equal "format: 1\nname: t\nagainst: a 1\nagainst: b 2\n", text
    assert_equal ["a 1", "b 2"], Record.parse(text)["against"]
  end

  def test_no_file_is_no_record
    Dir.mktmpdir { |d| assert_nil Record.read(Pathname(d) / "none") }
    assert_nil Record.format_of(nil)
  end
end

class TestInstallRecord < Minitest::Test

  include TestHelper

  def setup = reset_pkgmgr!

  # What a build writes beside the install: every field, the legacy
  # pair absent.
  def test_a_build_writes_the_whole_record
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("dep"))
        t = FakePackage.new("t", dep_list: [Dep("dep", false)])
        pkgmgr.register(t)
        rc, out = run_cli("-s", "t", "-q")
        assert_equal 0, rc, out

        inst = bound(t).find_install(Ver("1.0.0"))
        kv = Record.read(inst.path / InstallRecord::FILE)
        assert_equal ["1"], kv["format"]
        assert_equal ["t"], kv["name"]
        assert_equal ["1.0.0"], kv["version"]
        assert_equal [inst.coords.to_s], kv["coords"]
        assert_equal ["default"], kv["origin"]
        assert_equal ["manual"], kv["mark"]
        assert_equal [Host.env.to_s], kv["host"]
        assert_equal ["gcc-#{ARCH.gcc_ver}"], kv["stack"], "a target's stack"
        assert_includes kv["against"], "dep 1.0.0"
        assert_equal %w[.build_inputs .install],
                     Dir.children(inst.path).select { |f| f.start_with?(".") }
                        .sort
        assert_match(/\Aformat: /, (inst.path / BuildInputs::FILE).read)
      end
    end
  end

  def test_a_host_install_of_no_stack_says_none
    with_fake_tc do
      with_stubbed_externals do
        h = FakePackage.new("host_h", on_host: true, host_tier: :distro)
        pkgmgr.register(h)
        run_cli("-s", "host_h", "-q")
        kv = Record.read(bound(h).find_install(Ver("1.0.0")).path /
                         InstallRecord::FILE)
        assert_equal [], kv["stack"]
        assert_equal ["default"], kv["origin"]
      end
    end
  end

  # The legacy pair reads into the same keys, one-word files included,
  # and an install with neither reads as a manual default.
  def test_the_legacy_pair_reads_as_the_record
    Dir.mktmpdir do |d|
      dir = Pathname(d)
      File.write(dir / InstallRecord::LEGACY_ORIGIN, "pinned auto\n")
      File.write(dir / InstallRecord::LEGACY_DEPS, "host_gmp 6.1.0\nx 2.0.0\n")
      refute InstallRecord.default_install?(dir)
      refute InstallRecord.manual?(dir)
      assert_equal({ "host_gmp" => Ver("6.1.0"), "x" => Ver("2.0.0") },
                   InstallRecord.against(dir))
      refute InstallRecord.current?(dir)

      File.write(dir / InstallRecord::LEGACY_ORIGIN, "pinned\n")
      assert InstallRecord.manual?(dir), "one word: manual"

      FileUtils.rm_f(dir / InstallRecord::LEGACY_ORIGIN)
      FileUtils.rm_f(dir / InstallRecord::LEGACY_DEPS)
      assert InstallRecord.default_install?(dir)
      assert InstallRecord.manual?(dir)
      assert_equal({}, InstallRecord.against(dir))
    end
  end

  # .install wins over a legacy pair left beside it, and a re-mark
  # rewrites the record and removes the pair.
  def test_the_record_wins_and_a_remark_retires_the_pair
    Dir.mktmpdir do |d|
      dir = Pathname(d)
      File.write(dir / InstallRecord::LEGACY_ORIGIN, "pinned auto\n")
      InstallRecord.write(dir, name: "t", ver: Ver("1.0.0"),
                          coords: Coords.new("noarch", nil, nil),
                          default_install: true, manual: true, host: nil,
                          stack: nil, against: { "a" => Ver("1") })
      refute (dir / InstallRecord::LEGACY_ORIGIN).exist?, "write retires it"
      assert InstallRecord.default_install?(dir)

      File.write(dir / InstallRecord::LEGACY_ORIGIN, "pinned auto\n")
      assert InstallRecord.default_install?(dir), ".install wins"

      InstallRecord.remark(dir, false, false)
      refute (dir / InstallRecord::LEGACY_ORIGIN).exist?
      refute InstallRecord.default_install?(dir)
      refute InstallRecord.manual?(dir)
      kv = Record.read(dir / InstallRecord::FILE)
      assert_equal ["t"], kv["name"], "a re-mark keeps the rest"
      assert_equal ["a 1"], kv["against"]
    end
  end

  # A tree from before: the executor's first run writes .install for
  # every install from the pair and the path, the host unsaid, and
  # rewrites .build_inputs in the current spelling.
  def test_an_older_tree_is_brought_forward_by_the_first_run
    with_fake_tc do
      with_stubbed_externals do
        t = FakePackage.new("t")
        h = FakePackage.new("host_s", on_host: true, host_tier: :stack,
                            arch_list: ALL_HOST_ARCHS.values)
        [t, h].each { |p| pkgmgr.register(p) }
        dirs = [fake_install(t, mark: :auto), fake_install(h, origin: :pinned)]
        for dir in dirs do
          # Back to the pair, and the older .build_inputs spelling.
          kv = Record.read(dir / InstallRecord::FILE)
          FileUtils.rm_f(dir / InstallRecord::FILE)
          File.write(dir / InstallRecord::LEGACY_ORIGIN,
                     "#{kv["origin"].first} #{kv["mark"].first}\n")
          File.write(dir / InstallRecord::LEGACY_DEPS, "dep 1.0.0\n")
          bi = BuildInputs.read_any((dir / BuildInputs::FILE).read)
          File.write(dir / BuildInputs::FILE,
                     "recipe #{bi["recipe"].first}\nformat 3\n")
        end
        pkgmgr.installs_changed!

        run_cli("--mark-manual", "t", "-q")     # any executor run

        for dir, pkg in dirs.zip([t, h]) do
          assert InstallRecord.current?(dir), "#{pkg.name}: no .install"
          refute (dir / InstallRecord::LEGACY_ORIGIN).exist?
          refute (dir / InstallRecord::LEGACY_DEPS).exist?
          kv = Record.read(dir / InstallRecord::FILE)
          assert_equal [pkg.name], kv["name"]
          assert_equal ["dep 1.0.0"], kv["against"], "the pair's deps kept"
          assert_equal [], kv["host"], "nobody wrote the host down"
          assert_match(/\Aformat: #{BuildInputs::FORMAT}/,
                       (dir / BuildInputs::FILE).read)
        end
        assert_equal ["gcc-#{ARCH.gcc_ver}"],
                     Record.read(dirs[0] / InstallRecord::FILE)["stack"]
        assert_equal ["gcc-#{pkgmgr.default_stack_cc_ver}"],
                     Record.read(dirs[1] / InstallRecord::FILE)["stack"]
        assert_equal ["pinned"],
                     Record.read(dirs[1] / InstallRecord::FILE)["origin"]
        assert_equal ["manual"],
                     Record.read(dirs[0] / InstallRecord::FILE)["mark"],
                     "the re-mark itself"
      end
    end
  end

  # An install the world does not claim -- under another env of this
  # host, where a distro move stranded it -- is exactly what a record
  # is for, and gets one from its path and its legacy pair.
  def test_an_install_the_world_does_not_see_is_brought_forward_too
    with_fake_tc do
      with_stubbed_externals do
        h = FakePackage.new("host_h", on_host: true, host_tier: :distro)
        t = FakePackage.new("t")
        [h, t].each { |p| pkgmgr.register(p) }
        old = Coords.new(HOST_OS_ARCH, "olddistro-1.0", nil)
        dir = bound(h).pkg_dir_at(old) / "1.0.0"
        FileUtils.mkdir_p(dir)
        File.write(dir / InstallRecord::LEGACY_ORIGIN, "pinned auto\n")
        File.write(dir / InstallRecord::LEGACY_DEPS, "host_x 2.0.0\n")
        pkgmgr.refresh
        assert_empty pkgmgr.world.of("host_h"), "the world sees it?"

        run_cli("-s", "t", "-q")              # any executor run

        assert InstallRecord.current?(dir)
        kv = Record.read(dir / InstallRecord::FILE)
        assert_equal ["host_h"], kv["name"], "the registry's name, not h"
        assert_equal ["1.0.0"], kv["version"]
        assert_equal [old.to_s], kv["coords"]
        assert_equal ["pinned"], kv["origin"]
        assert_equal ["auto"], kv["mark"]
        assert_equal ["host_x 2.0.0"], kv["against"]
        assert_equal [], kv["stack"]
        refute (dir / InstallRecord::LEGACY_ORIGIN).exist?
      end
    end
  end

  # A :compiler-tier install carries the HOST's compiler as its third
  # coordinate, which is no stack of ours: the record names none. A
  # :stack one under another env names the stack it is in.
  def test_the_stack_of_an_unclaimed_install_follows_its_tier
    with_fake_tc do
      with_stubbed_externals do
        c = FakePackage.new("host_c", on_host: true, host_tier: :compiler)
        s = FakePackage.new("host_s", on_host: true, host_tier: :stack,
                            arch_list: ALL_HOST_ARCHS.values)
        t = FakePackage.new("t")
        [c, s, t].each { |p| pkgmgr.register(p) }
        cdir = bound(c).pkg_dir_at(Coords.new(HOST_OS_ARCH, "olddistro-1.0",
                                              "gcc-9.9.9")) / "1.0.0"
        sdir = bound(s).pkg_dir_at(Coords.new("linux-otherarch", nil,
                                              "gcc-8.8.8")) / "1.0.0"
        for d in [cdir, sdir] do
          FileUtils.mkdir_p(d)
          File.write(d / InstallRecord::LEGACY_ORIGIN, "default manual\n")
        end

        run_cli("-s", "t", "-q")

        assert_equal [], Record.read(cdir / InstallRecord::FILE)["stack"]
        assert_equal ["gcc-8.8.8"],
                     Record.read(sdir / InstallRecord::FILE)["stack"]
      end
    end
  end
end
