# SPDX-License-Identifier: BSD-2-Clause
#
# The arms nobody took.
#
# Branch coverage says 64%, and the missing third is not spread
# evenly: it is almost entirely error paths and alternate inputs --
# what happens when the package is an orphan, when the arch is not
# supported, when the thing being asked about is not installed at all.
# Those are the paths a person meets on a bad day, which is the worst
# time to find out they were never run.
#
# This file works that list. Each test names the branch it exists for.
#

require_relative 'test_helper'

class TestErrorPaths < Minitest::Test

  include TestHelper

  def setup
    reset_pkgmgr!
  end

  # package_manager.rb: python_interpreter raises when host_python is
  # not installed. Every meson-driven build goes through this, so the
  # message has to say what is missing rather than fail later as a
  # wrapper that cannot start.
  def test_asking_for_the_interpreter_without_one_installed
    with_fake_tc do
      err = assert_raises(RuntimeError) { pkgmgr.python_interpreter }
      assert_match(/host_python is not installed/, err.message)
    end
  end

  # force_remove has no package object to ask when the name belongs to
  # an orphan -- an install on disk that no registered package claims,
  # left by a rename. It falls back to every version at the current
  # coordinates, which is the only thing it can mean.
  def test_force_removing_an_orphan
    with_fake_tc do
      with_stubbed_externals do
        dir = target_pkgs(ARCH, FAKE_GCC_VER.to_s) / "ghost" / "1.0.0"
        FileUtils.mkdir_p(dir)
        pkgmgr.refresh()

        assert dir.directory?, "the fixture did not create the orphan"
        pkgmgr.force_remove("ghost")
        refute dir.directory?, "an orphan cannot be force-removed"
      end
    end
  end

  # get_stale_packages skips what this context cannot build. Without
  # the skip it would ask a package about installs it can never have,
  # and report a rebuild nobody can perform.
  def test_staleness_skips_packages_this_context_cannot_build
    with_fake_tc do
      with_stubbed_externals do
        here = FakePackage.new("here")
        nowhere = FakePackage.new("nowhere",
                                  arch_list: [ALL_ARCHS["riscv64"]])
        pkgmgr.register(here)
        pkgmgr.register(nowhere)
        pkgmgr.install("here")
        pkgmgr.refresh()

        names = with_context(ARCH: ALL_ARCHS["i386"], BOARD: nil) {
          pkgmgr.get_stale_packages.map(&:name)
        }
        refute_includes names, "nowhere",
                        "reported a rebuild this arch cannot perform"
      end
    end
  end

  # packages_in_stack answers about a stack that was never built.
  # Zero, not a crash: -L lists every version the compiler package
  # can build, including the ones nobody has asked for yet.
  def test_counting_packages_in_a_stack_that_does_not_exist
    with_fake_tc do
      assert_equal 0, pkgmgr.packages_in_stack(Ver("9.9.9"))
    end
  end

  # uninstall takes a package OBJECT as well as a name. Both are used:
  # the CLI has a string, internal callers have the package.
  def test_uninstall_accepts_a_package_object
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("objy")
        pkgmgr.register(pkg)
        pkgmgr.install("objy")
        pkgmgr.refresh()

        inst = pkg.get_install_list.find { |i| !i.path.nil? }
        refute_nil inst

        pkgmgr.uninstall(pkg, false, false)
        refute inst.path.directory?,
               "passing the package object removed nothing"
      end
    end
  end

  # install refuses an arch the package does not list, rather than
  # building into coordinates the package says nothing about.
  def test_installing_for_an_unsupported_arch_is_refused
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("rvonly", arch_list: [ALL_ARCHS["riscv64"]])
        pkgmgr.register(pkg)

        ok = with_context(ARCH: ALL_ARCHS["i386"], BOARD: nil) {
          pkgmgr.install("rvonly")
        }
        refute ok, "installed a package for an arch it does not support"
      end
    end
  end

  # --- token expansion --------------------------------------------

  # $PYTHON resolves to the interpreter we installed. The token is
  # what gets recorded in a recipe digest, so this is the only place
  # the path itself appears.
  def test_the_python_token_expands_to_our_interpreter
    with_fake_tc do
      with_stubbed_externals do
        py = FakePackage.new("host_python", on_host: true,
                             host_tier: :distro,
                             arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(py)
        pkgmgr.install("host_python")
        pkgmgr.refresh()

        pkg = FakePackage.new("user")
        pkgmgr.register(pkg)

        out = pkg.expand_tokens("run $PYTHON now", Pathname.new("/tmp"))
        refute_includes out, "$PYTHON", "the token was left unexpanded"
        assert_includes out, "bin/python3"
      end
    end
  end

  # $SYSROOT is a host-stack notion. A target package has none, and
  # the token has to come out empty rather than naming a stack that
  # has nothing to do with it.
  def test_the_sysroot_token_is_empty_for_a_target_package
    with_fake_tc do
      pkg = FakePackage.new("targety")
      pkgmgr.register(pkg)

      out = pkg.expand_tokens("[$SYSROOT]", Pathname.new("/tmp"))
      assert_equal "[]", out
    end
  end

  # build_files is the patch list, and most packages have no patches.
  def test_a_package_with_no_patch_directory_has_no_patch_files
    with_fake_tc do
      pkg = FakePackage.new("nopatch")
      pkgmgr.register(pkg)

      pkg.define_singleton_method(:patch_root) {
        Pathname.new("/nonexistent-patch-root")
      }
      assert_empty pkg.build_files(Ver("1.0.0"))
    end
  end

  # --- guards that fail loudly --------------------------------------

  # A stack coordinate built from a nil compiler version would be the
  # bare string "gcc-", and every arch would share one directory. The
  # guard exists because that silently merged two trees.
  def test_a_target_coords_without_a_compiler_version_raises
    with_fake_tc do
      pkg = FakePackage.new("archy")
      pkgmgr.register(pkg)

      arch = ALL_ARCHS["i386"]
      old = arch.gcc_ver

      begin
        arch.gcc_ver = nil
        err = assert_raises(RuntimeError) {
          with_context(ARCH: arch, BOARD: nil) { pkg.coords(Ver("1.0.0")) }
        }
        assert_match(/not.*set yet|gcc-/, err.message)
      ensure
        arch.gcc_ver = old
      end
    end
  end

  # Building a :stack package before the toolchain exists has to say
  # which toolchain is missing, not fail inside a compiler invocation
  # with the environment half set up.
  def test_building_a_stack_package_without_a_toolchain_raises
    with_fake_tc do
      pkg = FakePackage.new("host_thing", on_host: true,
                            host_tier: :stack,
                            arch_list: ALL_HOST_ARCHS.values)
      pkgmgr.register(pkg)

      err = assert_raises(RuntimeError) { pkg.stack_toolchain_bins }
      assert_match(/host toolchain is not installed/, err.message)
    end
  end

  # An unknown name is not an install: it has to be refused, and say
  # so, rather than resolving to something that happens to be nearby.
  def test_installing_an_unknown_package_is_refused
    with_fake_tc do
      refute pkgmgr.install("no_such_package_anywhere")
    end
  end
end
