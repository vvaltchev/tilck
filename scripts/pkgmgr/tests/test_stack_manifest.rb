# SPDX-License-Identifier: BSD-2-Clause
#
# THE STACK MANIFEST: written by the compilers that define stacks,
# read by the listing and the sysroot composition, and backfilled for
# stacks from before the record.
#
# A stack's compiler was found by a naming coincidence: the stack
# gcc-14.4.0 was host_gcc 14.4.0's because both spell 14.4.0, and the
# compiler was looked for where THIS invocation would put it. When the
# distro env moved, six stacks read as not built while their
# sysroots still held the grafted compiler. The manifest records the
# fact, for host stacks and target stacks alike.
#

require_relative 'test_helper'
require_relative '../stack_manifest'
require_relative '../host_gcc'
require_relative '../gcc'
require_relative '../executor'

class TestStackManifestValue < Minitest::Test

  include TestHelper

  def setup = reset_pkgmgr!

  def host_manifest
    StackManifest.new(kind: :host, compiler_name: "host_gcc",
                      compiler_ver: Ver("14.4.0"),
                      compiler_at: "linux-x86_64/omarchy-4.0.3/any",
                      libc: "host_glibc", libc_ver: Ver("2.41"),
                      host: "linux-x86_64 omarchy-4.0.3 gcc-16.2.1")
  end

  def test_a_manifest_round_trips_through_its_file
    with_fake_tc do
      c = Coords.new("linux-x86_64", nil, "gcc-14.4.0")
      assert_nil StackManifest.read(c), "nothing written yet"
      StackManifest.write(c, host_manifest)
      assert_equal host_manifest, StackManifest.read(c)
      assert_equal Coords.new("linux-x86_64", "omarchy-4.0.3", nil),
                   StackManifest.read(c).compiler_coords
      assert_equal "host_gcc 14.4.0", StackManifest.read(c).to_s
    end
  end

  # A target stack's manifest names no place and no host.
  def test_a_target_manifest_names_its_compiler_by_identity
    with_fake_tc do
      c = Coords.new("tilck-riscv64", "qemu-virt", "gcc-13.3.0")
      m = StackManifest.new(kind: :target, compiler_name: "gcc-riscv64-musl",
                            compiler_ver: Ver("13.3.0"), compiler_at: nil,
                            libc: "musl", libc_ver: Ver("1.2.5"), host: nil)
      StackManifest.write(c, m)
      text = (c.root / StackManifest::FILE).read
      refute_match(/compiler_at|host /, text)
      assert_equal m, StackManifest.read(c)
      assert_nil StackManifest.read(c).compiler_coords
    end
  end

  # Unknown keys are for a later format to add; a format this reader
  # does not know is no manifest.
  def test_unknown_keys_are_ignored_and_an_unknown_format_is_none
    with_fake_tc do
      c = Coords.new("linux-x86_64", nil, "gcc-14.4.0")
      FileUtils.mkdir_p(c.root)
      File.write(c.root / StackManifest::FILE,
                 host_manifest.render + "# a note\nflags -flto\n")
      assert_equal host_manifest, StackManifest.read(c)

      File.write(c.root / StackManifest::FILE,
                 host_manifest.render.sub("format 1", "format 2"))
      assert_nil StackManifest.read(c)
    end
  end

  # The libc is recorded with its version or not at all: half of it
  # would read back as the other half.
  def test_a_libc_without_a_version_is_not_recorded
    with_fake_tc do
      c = Coords.new("linux-x86_64", nil, "gcc-14.4.0")
      for libc, ver in [["host_glibc", nil], [nil, Ver("2.41")]] do
        m = host_manifest.with(libc: libc, libc_ver: ver)
        refute_match(/^libc/, m.render)
        StackManifest.write(c, m)
        back = StackManifest.read(c)
        assert_nil back.libc
        assert_nil back.libc_ver
      end
    end
  end

  # What the two compilers say they define.
  def test_host_gcc_defines_the_host_stack_of_its_version
    gcc = HostGccPackage.new
    pkgmgr.register(gcc)
    pairs = bound(gcc).stacks_defined(Ver("11.5.0"),
                                      { "host_glibc" => Ver("2.35") })
    assert_equal 1, pairs.length
    c, m = pairs.first
    assert_equal "#{HOST_OS_ARCH}/any/gcc-11.5.0", c.to_s
    assert_equal :host, m.kind
    assert_equal ["host_gcc", Ver("11.5.0")], [m.compiler_name, m.compiler_ver]
    assert_equal bound(gcc).coords(Ver("11.5.0")).to_s, m.compiler_at
    assert_equal ["host_glibc", Ver("2.35")], [m.libc, m.libc_ver]
    assert_equal scope.host.to_s, m.host
  end

  def test_a_cross_compiler_defines_the_target_stack_at_every_board
    rv = ALL_ARCHS["riscv64"]
    cc = GccCompiler.new(rv, "musl")
    pkgmgr.register(cc)
    pairs = bound(cc).stacks_defined(Ver("13.3.0"), {})
    assert_equal ["tilck-riscv64/licheerv-nano/gcc-13.3.0",
                  "tilck-riscv64/qemu-virt/gcc-13.3.0"],
                 pairs.map { |c, _| c.to_s }.sort
    m = pairs.first.last
    assert_equal [:target, "gcc-riscv64-musl", Ver("13.3.0"), nil, "musl"],
                 [m.kind, m.compiler_name, m.compiler_ver, m.compiler_at,
                  m.libc]
    assert_nil m.host
  end

  def test_a_package_that_is_no_compiler_defines_no_stack
    assert_empty bound(FakePackage.new("t")).stacks_defined(Ver("1.0.0"), {})
  end
end

# The executor writes manifests, and backfills stacks from before.
class TestStackManifestOnDisk < Minitest::Test

  include TestHelper

  # A fake compiler that defines a host stack, the way host_gcc does.
  class FakeStackCompiler < TestHelper::FakePackage
    def stacks_defined(ver, against, compiler_at: nil)
      stack = pkgmgr.stack_coords(ver, host: scope.host)
      m = StackManifest.new(kind: :host, compiler_name: name,
                            compiler_ver: ver,
                            compiler_at: (compiler_at || coords(ver)).to_s,
                            libc: "host_libc", libc_ver: against["host_libc"],
                            host: scope.host.to_s)
      return [[stack, m]]
    end
  end

  def setup = reset_pkgmgr!

  def compiler(deps: [])
    FakeStackCompiler.new("host_gcc", on_host: true, host_tier: :distro,
                          arch_list: ALL_HOST_ARCHS.values, dep_list: deps)
  end

  def plain(s) = s.gsub(/\e\[[0-9;]*m/, "")

  def test_a_build_of_a_compiler_writes_the_stack_it_defines
    with_fake_tc do
      with_stubbed_externals do
        libc = FakePackage.new("host_libc", on_host: true, host_tier: :stack,
                               arch_list: ALL_HOST_ARCHS.values)
        gcc = compiler(deps: [Dep("host_libc", true)])
        [libc, gcc].each { |p| pkgmgr.register(p) }
        v = pkgmgr.default_stack_cc_ver
        gcc.define_singleton_method(:default_ver) { v }

        rc, out = run_cli("-s", "host_gcc", "-q")
        assert_equal 0, rc, out

        m = StackManifest.read(pkgmgr.stack_coords(v))
        refute_nil m, "no manifest written for the stack"
        assert_equal ["host_gcc", v], [m.compiler_name, m.compiler_ver]
        assert_equal bound(gcc).coords(v).to_s, m.compiler_at
        assert_equal ["host_libc", Ver("1.0.0")], [m.libc, m.libc_ver]
        assert_equal scope.host.to_s, m.host
      end
    end
  end

  # A stack from before the record gets its manifest the first time
  # the executor runs, from the compiler install of its version.
  def test_a_stack_from_before_the_record_is_backfilled
    with_fake_tc do
      with_stubbed_externals do
        gcc = compiler
        t = FakePackage.new("t")
        [gcc, t].each { |p| pkgmgr.register(p) }
        v = pkgmgr.default_stack_cc_ver
        fake_install(gcc, v)
        FileUtils.mkdir_p(pkgmgr.stack_coords(v).pkgs_dir)
        assert_nil StackManifest.read(pkgmgr.stack_coords(v))

        run_cli("-s", "t", "-q")              # anything the executor runs

        m = StackManifest.read(pkgmgr.stack_coords(v))
        refute_nil m
        assert_equal bound(gcc).coords(v).to_s, m.compiler_at
      end
    end
  end

  # ...and when the distro env has moved since, the compiler is found
  # under the old env and recorded there: the case that made six
  # stacks read as not built.
  def test_a_compiler_stranded_under_an_old_env_is_found_and_recorded
    with_fake_tc do
      with_stubbed_externals do
        gcc = compiler
        t = FakePackage.new("t")
        [gcc, t].each { |p| pkgmgr.register(p) }
        v = pkgmgr.default_stack_cc_ver
        old = Coords.new(HOST_OS_ARCH, "olddistro-1.0", nil)
        dir = bound(gcc).pkg_dir_at(old) / v.to_s
        FileUtils.mkdir_p(dir)
        InstallOrigin.write(dir, true, true)
        FileUtils.mkdir_p(pkgmgr.stack_coords(v).pkgs_dir)
        assert_empty pkgmgr.world.of("host_gcc"), "the world sees it?"

        run_cli("-s", "t", "-q")

        m = StackManifest.read(pkgmgr.stack_coords(v))
        refute_nil m, "the stranded compiler was not found"
        assert_equal old.to_s, m.compiler_at
        assert_equal dir, bound(gcc).stack_compiler_dir(v),
                     "the locator does not follow the manifest"
        refute_nil bound(gcc).stack_compiler_dir(v)
      end
    end
  end

  # The listing reads the manifest: a stack whose compiler is where
  # the manifest says is built, wherever this invocation would put
  # the compiler today.
  def test_the_listing_believes_the_manifest
    with_fake_tc do
      with_stubbed_externals do
        gcc = compiler
        pkgmgr.register(gcc)
        v = pkgmgr.default_stack_cc_ver
        gcc.define_singleton_method(:installable_versions) { [v] }
        old = Coords.new(HOST_OS_ARCH, "olddistro-1.0", nil)
        dir = bound(gcc).pkg_dir_at(old) / v.to_s
        FileUtils.mkdir_p(dir)
        InstallOrigin.write(dir, true, true)
        FileUtils.mkdir_p(pkgmgr.stack_coords(v).pkgs_dir)

        out = plain(capture_io { pkgmgr.show_stacks }.join)
        assert_match(/^gcc-#{v}\s+\[\s+\]/, out, "not built, before")

        StackManifest.write(pkgmgr.stack_coords(v),
                            bound(gcc).stacks_defined(v, {}, compiler_at: old)
                                      .first.last)
        out = plain(capture_io { pkgmgr.show_stacks }.join)
        assert_match(/^gcc-#{v}\s+\[ built\s+\]/, out, "built, after")
      end
    end
  end

  # The Tilck stacks say their compiler too, from the target stack's
  # manifest, so the two tables answer the same question.
  def test_the_tilck_stacks_say_their_compiler
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("dflt", default: true))
        stack = register_tilck_stack!
        run_cli("-q")
        c = bound(stack).coords(stack.default_ver)
        StackManifest.write(c, StackManifest.new(
          kind: :target, compiler_name: "gcc-#{ARCH.name}-musl",
          compiler_ver: ARCH.gcc_ver, compiler_at: nil, libc: "musl",
          libc_ver: Ver("1.2.5"), host: nil))

        out = plain(capture_io { pkgmgr.show_tilck_stacks }.join)
        cc = "gcc-#{ARCH.name}-musl #{ARCH.gcc_ver}"
        assert_match(/^#{stack.name}\s+\[ built\s+\]\s+\d+ pkgs\s+#{cc}/, out)
      end
    end
  end
end
