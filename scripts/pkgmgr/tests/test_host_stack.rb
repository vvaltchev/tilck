# SPDX-License-Identifier: BSD-2-Clause
#
# The portable host tier: packages that link against our own glibc and
# nothing from the system. See docs/plans/portable-host-stack.md.
#

require_relative 'test_helper'

class TestPortableTier < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def portable_pkg(name = "host_h")
    return FakePackage.new(name, on_host: true, host_tier: :stack,
                           arch_list: ALL_HOST_ARCHS.values)
  end

  # The regression this guards is not subtle: the sysroot is REBUILT
  # from scratch whenever what it views changes, so composing it starts
  # by deleting it. A test whose portable paths escape the fake
  # toolchain therefore destroys the developer's real one — which is
  # what happened before with_fake_tc overrode HOST_DIR_PORTABLE.
  def test_host_stack_paths_stay_inside_the_fake_toolchain
    with_fake_tc do |tc|
      pkg = portable_pkg

      for path in [pkg.stack_root, pkg.stack_sysroot,
                   pkg.host_install_root]
        assert path.to_s.start_with?(tc.to_s + "/"),
               "#{path} escapes the fake toolchain at #{tc}"
      end
    end
  end

  # The stack is keyed by OUR compiler's version, not the distro or the
  # system compiler: neither takes part in the build.
  def test_install_root_is_keyed_by_our_gcc_version
    with_fake_tc do
      pkg = portable_pkg
      gcc_ver = pkgmgr.get_config_ver("gcc", host: true).to_s

      root = pkg.host_install_root.to_s
      assert_match(%r{/portable/gcc-#{Regexp.escape(gcc_ver)}\z}, root)

      # Neither the distro nor the system compiler appears: neither
      # takes part in the build, and the result runs without them.
      refute_match(/#{Regexp.escape(HOST_DISTRO)}/, root)
      refute_match(/#{Regexp.escape(HOST_CC)}/, root)
    end
  end

  # A package declared portable lands under portable/, which is the
  # promise that it runs on any host of this OS and architecture.
  def test_a_stack_package_lands_in_the_portable_tree
    with_fake_tc do
      root = portable_pkg.host_install_root.to_s
      assert_includes root, "/portable/"

      # Not under the distro either: that is the whole distinction.
      refute_includes root, HOST_DISTRO
    end
  end

  # The sysroot is a composed view, not an installation, so it lives
  # outside the package tree altogether -- every scanner that walks
  # <pkg>/<ver>/ would otherwise have to be taught to skip it, and one
  # of them was not.
  def test_sysroot_is_outside_the_package_tree
    with_fake_tc do
      pkg = portable_pkg
      refute pkg.stack_sysroot.to_s.start_with?(
               pkg.host_install_root.to_s + "/")
      assert_includes pkg.stack_sysroot.to_s, "/sysroots/"
    end
  end

  # Without HOST_VER_GCC every stack package would install to the
  # same truncated path, so this must fail loudly rather than build one.
  def test_missing_host_gcc_version_raises
    with_fake_tc do
      pkg = portable_pkg
      pm = PackageManager.instance
      orig = pm.method(:get_config_ver)
      begin
        pm.define_singleton_method(:get_config_ver) { |name, host:|
          name == "gcc" && host ? nil : orig.call(name, host: host)
        }
        e = assert_raises(RuntimeError) { pkg.stack_root }
        assert_match(/HOST_VER_GCC/, e.message)
      ensure
        pm.define_singleton_method(:get_config_ver, orig)
      end
    end
  end
end

class TestPortableGating < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # A disabled package is invisible, not merely absent from the default
  # set: building gcc and glibc takes tens of minutes and must be asked
  # for, never stumbled into.
  def test_disabled_package_is_not_installable
    with_fake_tc do
      pkg = FakePackage.new("host_off", on_host: true,
                            arch_list: ALL_HOST_ARCHS.values)
      pkg.define_singleton_method(:enabled?) { false }
      pkgmgr.register(pkg)

      assert_empty pkg.get_installable_list
    end
  end

  def test_enabled_package_is_installable
    with_fake_tc do
      pkg = FakePackage.new("host_on", on_host: true,
                            arch_list: ALL_HOST_ARCHS.values)
      pkgmgr.register(pkg)

      refute_empty pkg.get_installable_list
    end
  end

  def test_install_refuses_a_disabled_package
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_off", on_host: true,
                              arch_list: ALL_HOST_ARCHS.values)
        pkg.define_singleton_method(:enabled?) { false }
        pkgmgr.register(pkg)

        refute pkgmgr.install("host_off")
        assert_empty FakePackage.install_log
      end
    end
  end

  def test_packages_are_enabled_by_default
    assert FakePackage.new("foo").enabled?
  end
end

class TestFinalInstallPrefix < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # A package that bakes an absolute --prefix into its output must be
  # given the path it will END UP at. Baking the staging path leaves it
  # pointing at a directory that stops existing the moment the atomic
  # move completes -- which is exactly what binutils did before this.
  def test_prefix_is_the_final_path_not_the_staging_one
    with_fake_tc do
      pkg = FakePackage.new("host_foo", on_host: true, host_tier: :distro)
      staging = pkg.staging_dir(Ver("1.0.0"))

      got = pkg.final_install_prefix(staging).to_s

      refute_match(/staging/, got)
      assert_match(%r{/foo/1\.0\.0/install\z}, got)
      assert got.start_with?(pkg.final_install_root.to_s)
    end
  end

  def test_prefix_follows_the_staged_version
    with_fake_tc do
      pkg = FakePackage.new("host_foo", on_host: true, host_tier: :distro)
      got = pkg.final_install_prefix(pkg.staging_dir(Ver("2.5.0"))).to_s
      assert_match(%r{/foo/2\.5\.0/install\z}, got)
    end
  end
end

#
# Which stack a package belongs to.
#
# The stack is keyed by compiler version so that libraries built by
# compiler X live with compiler X — the C++ ABI is not stable across
# majors. A compiler bound to a DIFFERENT compiler's stack defeats
# exactly that, and it is what `-s host_gcc:11.5.0` produced before
# this: a compiler installed as 11.5.0 but configured
# --with-sysroot=.../gcc-14.4.0/ with 14.4.0's loader in its specs.
#
class TestPortableStackBinding < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # Everything except the compiler belongs to the stack the world is
  # currently being built for.
  def test_an_ordinary_package_uses_the_default_stack
    with_fake_tc do
      pkg = FakePackage.new("host_h", on_host: true, host_tier: :stack,
                            arch_list: ALL_HOST_ARCHS.values)
      assert_equal pkgmgr.default_stack_cc_ver, pkg.stack_gcc_ver
      assert_equal pkgmgr.stack_root.to_s, pkg.stack_root.to_s
    end
  end

  # Asking for a version is a request to BUILD that version. The
  # default exists only so that a version can be omitted; it never
  # limits what can be built or what can coexist.
  def test_the_scope_decides_the_stack_not_the_default
    with_fake_tc do
      pkg = FakePackage.new("host_h", on_host: true, host_tier: :stack,
                            arch_list: ALL_HOST_ARCHS.values)

      default = pkgmgr.default_stack_cc_ver
      assert_equal default, pkg.stack_gcc_ver

      pkgmgr.with_host_stack(Ver("13.4.0")) do
        assert_equal Ver("13.4.0"), pkg.stack_gcc_ver
        assert pkg.stack_root.to_s.end_with?("/portable/gcc-13.4.0")
        assert pkg.host_install_root.to_s.end_with?("/portable/gcc-13.4.0")
      end

      # ...and the scope is scoped.
      assert_equal default, pkg.stack_gcc_ver
    end
  end

  # The point of the whole thing: a package installed in one stack is
  # NOT installed as far as another stack is concerned, so requesting a
  # different compiler pulls its own glibc and headers into the plan
  # rather than borrowing the ones next door.
  def test_an_install_in_one_stack_is_absent_from_another
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_h", on_host: true, host_tier: :stack,
                              arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(pkg)
        pkg.install_impl(Ver("1.0.0"))

        assert pkg.installed?(Ver("1.0.0"))

        pkgmgr.with_host_stack(Ver("13.4.0")) do
          refute pkg.installed?(Ver("1.0.0"))
        end
      end
    end
  end

  def test_nested_scopes_restore
    with_fake_tc do
      pkg = FakePackage.new("host_h", on_host: true, host_tier: :stack,
                            arch_list: ALL_HOST_ARCHS.values)
      pkgmgr.with_host_stack(Ver("11.5.0")) do
        pkgmgr.with_host_stack(Ver("16.2.0")) do
          assert_equal Ver("16.2.0"), pkg.stack_gcc_ver
        end
        assert_equal Ver("11.5.0"), pkg.stack_gcc_ver
      end
    end
  end

  # The compiler belongs to its own.
  def test_a_compiler_uses_its_own_version
    with_fake_tc do
      pkg = FakePackage.new("host_cc", on_host: true, host_tier: :distro,
                            arch_list: ALL_HOST_ARCHS.values)
      pkg.define_singleton_method(:stack_gcc_ver) { |v = nil| v || Ver("9.9.9") }

      assert_equal Ver("9.9.9"), pkg.stack_gcc_ver
      assert_equal Ver("1.2.3"), pkg.stack_gcc_ver(Ver("1.2.3"))
    end
  end

  def test_the_root_follows_the_version_asked_for
    with_fake_tc do
      a = pkgmgr.stack_root(Ver("11.5.0")).to_s
      b = pkgmgr.stack_root(Ver("14.4.0")).to_s

      refute_equal a, b
      assert a.end_with?("/portable/gcc-11.5.0")
      assert b.end_with?("/portable/gcc-14.4.0")
    end
  end

  def test_each_stack_has_its_own_sysroot
    with_fake_tc do
      a = pkgmgr.stack_sysroot(Ver("11.5.0")).to_s
      b = pkgmgr.stack_sysroot(Ver("14.4.0")).to_s
      refute_equal a, b
      assert a.end_with?("/sysroots/#{HOST_OS}-#{HOST_ARCH.name}/gcc-11.5.0")
    end
  end

  # A sysroot is a VIEW over installed packages, not an installation,
  # so it lives outside the package tree entirely. Inside it, every
  # scanner that walks <pkg>/<ver>/ had to be taught to skip it -- and
  # one of them was not, which is how the sysroot once got listed as a
  # package called "sysroot" at version "usr".
  def test_the_sysroot_is_not_inside_the_package_tree
    with_fake_tc do
      pkgs = pkgmgr.stack_root(Ver("14.4.0")).to_s
      sysroot = pkgmgr.stack_sysroot(Ver("14.4.0")).to_s

      refute sysroot.start_with?(pkgs),
             "#{sysroot} must not sit under #{pkgs}"
    end
  end

  # Composing one stack must not pull in packages installed under
  # another: their libraries were built by a different compiler.
  def test_a_package_is_only_a_fragment_of_its_own_stack
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("host_h", on_host: true, host_tier: :stack,
                              arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(pkg)
        pkg.install_impl(Ver("1.0.0"))

        own = pkgmgr.default_stack_cc_ver
        refute_empty pkg.sysroot_fragments(own)
        assert_empty pkg.sysroot_fragments(Ver("9.9.9"))
      end
    end
  end

  def test_stacks_on_disk_are_discoverable
    with_fake_tc do
      FileUtils.mkdir_p(HOST_DIR_PORTABLE / "gcc-11.5.0")
      FileUtils.mkdir_p(HOST_DIR_PORTABLE / "gcc-14.4.0")
      FileUtils.mkdir_p(HOST_DIR_PORTABLE / "not-a-stack")

      assert_equal ["11.5.0", "14.4.0"], pkgmgr.host_stacks.sort
    end
  end
end

#
# The build environment stack packages compile in.
#
class TestPortableToolchainEnv < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # Replacing pkg-config's search path means replacing ALL of it.
  # Architecture-independent packages install to share/pkgconfig —
  # xorgproto does — and listing only lib/pkgconfig loses them:
  # libXau failed with "No package 'xproto' found" while xproto.pc was
  # sitting in the sysroot.
  def test_pkg_config_covers_both_standard_directories
    with_fake_tc do
      pkg = FakePackage.new("host_h", on_host: true, host_tier: :stack,
                            arch_list: ALL_HOST_ARCHS.values)
      sysroot = pkg.stack_sysroot.to_s

      # Read the value the same way the build does, without needing the
      # toolchain to be installed.
      dirs = ["#{sysroot}/usr/lib/pkgconfig",
              "#{sysroot}/usr/share/pkgconfig"]

      for d in dirs
        assert d.start_with?(sysroot),
               "every pkg-config dir must be inside the sysroot"
      end

      refute_equal dirs[0], dirs[1]
    end
  end
end
