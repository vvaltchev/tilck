# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'

class TestUninstallSingle < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_uninstall_removes_version_dir
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        pkgmgr.refresh()

        gcc = FAKE_GCC_VER.to_s
        ver_dir = target_pkgs(ARCH, gcc) / "foo" / "1.0.0"
        assert ver_dir.directory?

        pkgmgr.uninstall("foo", false, false)
        refute ver_dir.exist?
      end
    end
  end

  def test_uninstall_cleans_empty_parents
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        pkgmgr.refresh()

        gcc = FAKE_GCC_VER.to_s
        pkg_dir = target_pkgs(ARCH, gcc) / "foo"
        pkgmgr.uninstall("foo", false, false)
        refute pkg_dir.exist?
      end
    end
  end

  def test_uninstall_dry_run
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        pkgmgr.refresh()

        gcc = FAKE_GCC_VER.to_s
        ver_dir = target_pkgs(ARCH, gcc) / "foo" / "1.0.0"
        pkgmgr.uninstall("foo", true, false)  # dry = true
        assert ver_dir.directory?  # still there
      end
    end
  end

  def test_uninstall_unknown_package_no_crash
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.refresh()
        pkgmgr.uninstall("nonexistent", false, false)
        # Should not raise — just warns and does nothing
      end
    end
  end

  def test_uninstall_noarch_package
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("noarch_foo", arch_list: nil)
        pkgmgr.register(pkg)

        # Manually create the noarch install dir
        ver_dir = noarch_pkgs / "noarch_foo" / "1.0.0"
        FileUtils.mkdir_p(ver_dir)
        pkgmgr.refresh()

        pkgmgr.uninstall("noarch_foo", false, false)
        refute ver_dir.exist?
      end
    end
  end

  def test_uninstall_host_package
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true,
                              arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(pkg)
        pkgmgr.install("host_foo")
        pkgmgr.refresh()

        pkgmgr.uninstall("host_foo", false, false)
        refute pkg.installed?(Ver("1.0.0"))
      end
    end
  end
end

#
# Orphans: installations on disk that no registered package claims,
# e.g. what a package rename leaves behind. `-l` reports them as
# "found", so `-u <name>` has to be able to remove them.
#
class TestUninstallOrphans < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # Install a host package, then unregister it so its tree on disk
  # becomes unclaimed — exactly the state a rename produces. Note the
  # orphan is known by its DIRECTORY name, which is pkg_dirname (the
  # "host_" prefix stripped), not the package name.
  def make_host_orphan(tc)
    pkg = FakePackage.new("host_gone", on_host: true, host_tier: :distro)
    pkgmgr.register(pkg)
    pkgmgr.install("host_gone")
    reset_pkgmgr!
    pkgmgr.refresh()
    return distro_pkgs / "gone" / "1.0.0"
  end

  def test_orphan_is_discovered
    with_fake_tc do |tc|
      with_stubbed_externals do
        ver_dir = make_host_orphan(tc)
        assert ver_dir.directory?
        assert_includes pkgmgr.orphan_names, "gone"
      end
    end
  end

  def test_orphan_names_excludes_claimed_installs
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        pkgmgr.refresh()
        refute_includes pkgmgr.orphan_names, "foo"
      end
    end
  end

  # The regression: a host-side orphan has compiler "syscc" and the
  # host arch, while the fallback defaults describe the current TARGET.
  # Selecting on those defaults matched nothing and removed nothing,
  # silently, with exit status 0.
  def test_uninstall_removes_a_host_orphan
    with_fake_tc do |tc|
      with_stubbed_externals do
        ver_dir = make_host_orphan(tc)

        pkgmgr.uninstall("gone", false, false)
        refute ver_dir.exist?
      end
    end
  end

  def test_uninstall_orphan_dry_run_keeps_it
    with_fake_tc do |tc|
      with_stubbed_externals do
        ver_dir = make_host_orphan(tc)

        pkgmgr.uninstall("gone", true, false)
        assert ver_dir.directory?
      end
    end
  end

  # An explicit filter still narrows the selection: a target arch that
  # the host-side orphan does not have must not match it.
  def test_uninstall_orphan_respects_an_explicit_arch_filter
    with_fake_tc do |tc|
      with_stubbed_externals do
        ver_dir = make_host_orphan(tc)

        pkgmgr.uninstall("gone", false, false, nil, nil, "riscv64")
        assert ver_dir.directory?
      end
    end
  end
end

class TestUninstallALL < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_uninstall_all_default_arch
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("a"))
        pkgmgr.register(FakePackage.new("b"))
        pkgmgr.install("a")
        pkgmgr.install("b")
        pkgmgr.refresh()

        pkgmgr.uninstall("ALL", false, false)
        refute pkgmgr.get("a").installed?(Ver("1.0.0"))
        refute pkgmgr.get("b").installed?(Ver("1.0.0"))
      end
    end
  end

  def test_uninstall_all_excludes_compilers_without_force
    with_fake_tc do |tc|
      with_stubbed_externals do
        cc = FakePackage.new("gcc-#{ARCH.name}-musl",
                             on_host: true, is_compiler: true,
                             arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(cc)
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("gcc-#{ARCH.name}-musl")
        pkgmgr.install("foo")
        pkgmgr.refresh()

        pkgmgr.uninstall("ALL", false, false)  # force = false
        # Compiler should still be installed
        assert cc.installed?(Ver("1.0.0"))
        # Regular package should be removed
        refute pkgmgr.get("foo").installed?(Ver("1.0.0"))
      end
    end
  end

  def test_uninstall_all_includes_compilers_with_force
    with_fake_tc do |tc|
      with_stubbed_externals do
        cc = FakePackage.new("gcc-#{ARCH.name}-musl",
                             on_host: true, is_compiler: true,
                             host_tier: :portable,
                             arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(cc)
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("gcc-#{ARCH.name}-musl")
        pkgmgr.install("foo")
        pkgmgr.refresh()

        # Need -c ALL to also match host packages (compiler="syscc")
        pkgmgr.uninstall("ALL", false, true, nil, "ALL", "ALL")
        refute cc.installed?(Ver("1.0.0"))
        refute pkgmgr.get("foo").installed?(Ver("1.0.0"))
      end
    end
  end

  def test_uninstall_all_with_arch_filter
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")

        # Also create a "riscv64" install manually
        gcc = FAKE_GCC_VER.to_s
        rv_dir = target_pkgs(ALL_ARCHS["riscv64"], gcc) / "foo" / "1.0.0"
        FileUtils.mkdir_p(rv_dir)
        pkgmgr.refresh()

        # Uninstall only riscv64
        pkgmgr.uninstall("ALL", false, false, nil, nil, "riscv64")
        # riscv64 install should be gone
        refute rv_dir.exist?
        # i386 install should remain
        assert pkgmgr.get("foo").installed?(Ver("1.0.0"))
      end
    end
  end

  def test_uninstall_all_with_compiler_filter
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        pkgmgr.refresh()

        # Uninstall with compiler=ALL should include this package
        pkgmgr.uninstall("ALL", false, false, nil, "ALL", nil)
        refute pkgmgr.get("foo").installed?(Ver("1.0.0"))
      end
    end
  end
end

class TestUninstallVersions < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def test_uninstall_specific_version
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")

        # Also install a second "version" manually
        gcc = FAKE_GCC_VER.to_s
        v2_dir = target_pkgs(ARCH, gcc) / "foo" / "2.0.0"
        FileUtils.mkdir_p(v2_dir)
        pkgmgr.refresh()

        # Uninstall only version 1.0.0
        pkgmgr.uninstall("foo", false, false, Ver("1.0.0"))
        refute pkgmgr.get("foo").installed?(Ver("1.0.0"))
        assert v2_dir.directory?  # 2.0.0 still there
      end
    end
  end

  #
  # -f is an uninstall followed by an install, so the two halves have
  # to cover the same ground.
  #
  class TwoArchPackage < TestHelper::FakePackage
    def install_archs(ver = nil) =
      [ALL_ARCHS["i386"], ALL_ARCHS["x86_64"]]
  end

  def fake_install_at(arch, name)
    dir = target_pkgs(arch, FAKE_GCC_VER.to_s) / name / "1.0.0"
    FileUtils.mkdir_p(dir)
    return dir
  end

  # gnuefi's shape: one call builds i386 AND x86_64. Removing only the
  # current arch left the other behind, and the reinstall then died on
  # a directory it expected to create.
  def test_force_remove_covers_every_arch_the_install_writes
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(TwoArchPackage.new("twoarch"))
        i386 = fake_install_at(ALL_ARCHS["i386"], "twoarch")
        x64  = fake_install_at(ALL_ARCHS["x86_64"], "twoarch")
        pkgmgr.refresh()

        pkgmgr.force_remove("twoarch")
        refute i386.directory?, "the current arch was not removed"
        refute x64.directory?, "the other arch this install writes stayed"
      end
    end
  end

  # Several versions of one package coexist on purpose -- six gcc
  # majors in one directory -- and a forced rebuild of one must not
  # take the others. It did: `-s host_gcc:16.2.0 -f` removed all six
  # before building one, so a loop over the majors destroyed each
  # previous build.
  def test_force_remove_takes_only_the_version_asked_for
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("multi"))
        dirs = ["1.0.0", "2.0.0", "3.0.0"].to_h { |v|
          d = target_pkgs(ARCH, FAKE_GCC_VER.to_s) / "multi" / v
          FileUtils.mkdir_p(d)
          [v, d]
        }
        pkgmgr.refresh()

        pkgmgr.force_remove("multi", Ver("2.0.0"))

        refute dirs["2.0.0"].directory?, "the asked-for version stayed"
        assert dirs["1.0.0"].directory?, "an older version was destroyed"
        assert dirs["3.0.0"].directory?, "a newer version was destroyed"
      end
    end
  end

  #
  # Naming a version that is not installed removes NOTHING.
  #
  # uninstall falls back to "remove whatever version IS installed"
  # when the one it wants is absent. That is right when no version was
  # named and the default happens not to be there; it is catastrophic
  # when the user named one. `-u host_gcc:11.5.0` against a tree
  # holding six GCC majors took all six -- twice, because running the
  # same script a second time is exactly how you ask for a version
  # that was already removed.
  #
  def test_uninstalling_an_absent_version_takes_nothing
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("multi"))
        dirs = ["1.0.0", "2.0.0", "3.0.0"].to_h { |v|
          d = target_pkgs(ARCH, FAKE_GCC_VER.to_s) / "multi" / v
          FileUtils.mkdir_p(d)
          [v, d]
        }
        pkgmgr.refresh()

        n = pkgmgr.uninstall("multi", false, false, Ver("9.9.9"))

        assert_equal 0, n, "it removed something"
        for v, d in dirs do
          assert d.directory?, "version #{v} was destroyed"
        end
      end
    end
  end

  # ...but with NO version named, the fallback is still what we want:
  # the default is absent, so take what is actually installed.
  def test_no_version_named_still_falls_back_to_what_is_there
    with_fake_tc do
      with_stubbed_externals do
        # default_ver is 1.0.0 for a FakePackage; install only 2.0.0.
        pkgmgr.register(FakePackage.new("multi"))
        d = target_pkgs(ARCH, FAKE_GCC_VER.to_s) / "multi" / "2.0.0"
        FileUtils.mkdir_p(d)
        pkgmgr.refresh()

        pkgmgr.uninstall("multi", false, false)
        refute d.directory?, "the fallback stopped working"
      end
    end
  end

  # -f on a version that is not installed yet is just an install. It
  # must not fall through to uninstall's "remove whatever IS there".
  # -u has to find a :stack package's install.
  #
  # The compiler to match defaulted through `default_cc == "syscc"`,
  # which was the same question as "is this a host package" only while
  # every host package answered "syscc". A :stack package now answers
  # with the GCC whose stack it lives in, so the test stopped being
  # true and the uninstall went looking for an install built by the
  # i386 CROSS compiler -- matching nothing, and saying nothing.
  def test_uninstall_finds_a_stack_packages_install
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_thing", on_host: true,
                              host_tier: :stack,
                              arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(pkg)
        pkgmgr.install("host_thing")
        pkgmgr.refresh()

        inst = pkg.get_install_list.find { |i| !i.path.nil? }
        refute_nil inst, "the fixture did not install"

        pkgmgr.uninstall("host_thing", false, false)
        pkgmgr.refresh()

        refute inst.path.directory?, "-u matched nothing and said nothing"
      end
    end
  end

  # A host package's compiler is a version, not nil, and force_remove
  # passed nil for it -- which the filter reads as "the compiler must
  # BE nil", true only of a noarch package. So a forced rebuild of
  # anything else removed nothing, found its own install still there
  # and reported success:
  #
  #   INFO:   Force-removing: host_qemu:6.2.0
  #   INFO: All requested packages are already installed
  def test_force_remove_takes_a_host_package_too
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_thing", on_host: true,
                              host_tier: :stack,
                              arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(pkg)
        pkgmgr.install("host_thing")
        pkgmgr.refresh()

        inst = pkg.get_install_list.find { |i| !i.path.nil? }
        refute_nil inst, "the fixture did not install"
        assert inst.path.directory?

        pkgmgr.force_remove("host_thing")
        pkgmgr.refresh()

        refute inst.path.directory?,
               "a forced rebuild left the tree it was about to replace"
      end
    end
  end

  def test_force_remove_of_an_absent_version_removes_nothing
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("multi"))
        d = target_pkgs(ARCH, FAKE_GCC_VER.to_s) / "multi" / "1.0.0"
        FileUtils.mkdir_p(d)
        pkgmgr.refresh()

        pkgmgr.force_remove("multi", Ver("9.9.9"))
        assert d.directory?, "an unrelated version was destroyed"
      end
    end
  end

  # ...and no further, on EITHER axis. An arch covers all of its
  # boards at once, so this is the case the arch-level filter cannot
  # express: rebuilding zlib for one riscv64 board deleted the other
  # board's build and put nothing back, and nothing reported it -- a
  # package that is simply gone reads as "not installed", not "stale".
  def test_force_remove_leaves_the_other_board_alone
    with_fake_tc do
      with_stubbed_externals do
        rv = ALL_ARCHS["riscv64"]
        pkgmgr.register(FakePackage.new("boardy", arch_list: [rv]))

        dirs = rv.boards.to_h { |b|
          d = target_pkgs(rv, FAKE_GCC_VER.to_s, b) / "boardy" / "1.0.0"
          FileUtils.mkdir_p(d)
          [b, d]
        }
        pkgmgr.refresh()

        with_context(ARCH: rv, BOARD: "licheerv-nano") do
          pkgmgr.force_remove("boardy")
        end

        refute dirs["licheerv-nano"].directory?, "the target board stayed"
        assert dirs["qemu-virt"].directory?,
               "the other board's build was destroyed"
      end
    end
  end

  # ...and no further. Rebuilding the i386 zlib must not delete the
  # riscv64 one, which nothing is about to recreate.
  def test_force_remove_leaves_arches_the_install_does_not_write
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("onearch"))
        here  = fake_install_at(ARCH, "onearch")
        other = fake_install_at(ALL_ARCHS["riscv64"], "onearch")
        pkgmgr.refresh()

        pkgmgr.force_remove("onearch")
        refute here.directory?, "the current arch was not removed"
        assert other.directory?, "another arch's build was destroyed"
      end
    end
  end

  def test_uninstall_falls_back_to_all_versions
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        # Install at version 1.0.0 (default_ver)
        pkgmgr.install("foo")

        # Also add 2.0.0
        gcc = FAKE_GCC_VER.to_s
        v2_dir = target_pkgs(ARCH, gcc) / "foo" / "2.0.0"
        FileUtils.mkdir_p(v2_dir)
        pkgmgr.refresh()

        # Uninstall without specifying version — default_ver is 1.0.0,
        # which IS installed, so only 1.0.0 gets removed
        pkgmgr.uninstall("foo", false, false)
        refute pkgmgr.get("foo").installed?(Ver("1.0.0"))
        assert v2_dir.directory?  # 2.0.0 untouched
      end
    end
  end
end
