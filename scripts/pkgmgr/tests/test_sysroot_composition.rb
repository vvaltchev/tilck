# SPDX-License-Identifier: BSD-2-Clause
#
# Composing the sysroot of a stack that is NOT the current one.
#
# Every stack gets recomposed whenever an uninstall removes anything,
# from whatever invocation happened to do the removing:
#
#   host_stacks.each { |v| compose_stack_sysroot(Ver(v)) }
#
# so composition is asked about stacks the caller is not scoped to,
# and has to answer about the stack it was ASKED about.
#
# It did not. sysroot_fragments called find_install, which answers at
# the package's coordinates -- and for a stack package the stack IS a
# coordinate -- so it returned the CURRENT stack's install, whose path
# then failed the "must live in this stack" check and produced no
# fragments at all. Five populated sysroots were replaced with empty
# ones in a single uninstall, and the next compiler build stopped on:
#
#   The directory (BUILD_SYSTEM_HEADER_DIR) that should contain system
#   headers does not exist: .../gcc-16.2.0/sysroot/usr/include
#

require_relative 'test_helper'
require_relative '../sysroot'

class TestSysrootCompositionAcrossStacks < Minitest::Test

  include TestHelper

  OTHER = "13.4.0"

  def stack_pkg(name = "host_h")
    return FakePackage.new(name, on_host: true, host_tier: :stack,
                           arch_list: ALL_HOST_ARCHS.values)
  end

  # Install `pkg` into one specific stack, with a file in it so the
  # composed sysroot has something to link.
  def install_into(pkg, stack, rel = "usr/include/stdio.h")
    pkgmgr.with_host_stack(Ver(stack)) do
      dir = pkg.coords.pkgs_dir / pkg.pkg_dirname / "1.0.0" / "install"
      FileUtils.mkdir_p(File.dirname(dir / rel))
      File.write(dir / rel, "x")
      pkgmgr.installs_changed!
      pkgmgr.refresh()
    end
  end

  def test_fragments_come_from_the_stack_being_composed
    with_fake_tc do
      reset_pkgmgr!
      pkg = stack_pkg
      pkgmgr.register(pkg)
      install_into(pkg, OTHER)

      # Asked from OUTSIDE that stack, exactly as the recompose loop
      # does.
      frags = pkg.sysroot_fragments(Ver(OTHER))

      refute_empty frags,
                   "no fragment for the stack that actually has the package"
      assert frags.first.to_s.include?("gcc-#{OTHER}"),
             "the fragment came from a different stack: #{frags.first}"
    end
  end

  # ...and must NOT come from the current one.
  def test_fragments_do_not_leak_from_the_current_stack
    with_fake_tc do
      reset_pkgmgr!
      pkg = stack_pkg
      pkgmgr.register(pkg)
      install_into(pkg, pkgmgr.default_stack_cc_ver.to_s)

      # The package is installed in the DEFAULT stack only, so the
      # other stack has nothing to contribute.
      assert_empty pkg.sysroot_fragments(Ver(OTHER)),
                   "a fragment from another stack leaked in"
    end
  end

  def test_composing_another_stack_populates_it
    with_fake_tc do
      reset_pkgmgr!
      pkg = stack_pkg
      pkgmgr.register(pkg)
      install_into(pkg, OTHER)

      n = pkgmgr.compose_stack_sysroot(Ver(OTHER))

      assert_operator n, :>, 0, "composed nothing"
      assert File.exist?(pkgmgr.stack_sysroot(Ver(OTHER)) /
                         "usr" / "include" / "stdio.h")
    end
  end

  # The loop the uninstall path actually runs: compose EVERY stack,
  # from one invocation. Each must keep its own contents.
  def test_composing_every_stack_in_one_pass_keeps_them_all
    with_fake_tc do
      reset_pkgmgr!
      a = stack_pkg("host_a")
      b = stack_pkg("host_b")
      pkgmgr.register(a)
      pkgmgr.register(b)

      default = pkgmgr.default_stack_cc_ver.to_s
      install_into(a, default, "usr/include/a.h")
      install_into(b, OTHER, "usr/include/b.h")

      for v in [default, OTHER] do
        pkgmgr.compose_stack_sysroot(Ver(v))
      end

      assert File.exist?(pkgmgr.stack_sysroot(Ver(default)) /
                         "usr" / "include" / "a.h"), "default stack emptied"
      assert File.exist?(pkgmgr.stack_sysroot(Ver(OTHER)) /
                         "usr" / "include" / "b.h"), "other stack emptied"
    end
  end

  #
  # The guard. Nothing to compose over something already composed is
  # not "a stack with no packages" -- it is a question asked wrongly,
  # and answering it destroys a working stack.
  #
  def test_an_empty_composition_refuses_to_replace_a_populated_sysroot
    with_fake_tc do
      reset_pkgmgr!
      pkg = stack_pkg
      pkgmgr.register(pkg)
      install_into(pkg, OTHER)
      pkgmgr.compose_stack_sysroot(Ver(OTHER))

      root = pkgmgr.stack_sysroot(Ver(OTHER))
      assert File.exist?(root / "usr" / "include" / "stdio.h")

      # Now ask again with nothing registered: no fragments at all.
      reset_pkgmgr!
      n = pkgmgr.compose_stack_sysroot(Ver(OTHER))

      assert_equal 0, n
      assert File.exist?(root / "usr" / "include" / "stdio.h"),
             "the populated sysroot was emptied"
    end
  end

  # An empty stack that has never been composed is a different thing,
  # and must still be allowed: that is every stack's first install.
  def test_an_empty_composition_is_fine_when_there_is_nothing_there
    with_fake_tc do
      reset_pkgmgr!
      assert_equal 0, pkgmgr.compose_stack_sysroot(Ver(OTHER))
    end
  end

  #
  # ...and when the packages are genuinely GONE, emptying is right.
  #
  # The guard above must not fire here or a clean leaves a farm of
  # symlinks pointing at things it just uninstalled -- which is what
  # happened: --clean removed 104 installs and every stack kept its
  # stale sysroot, because "no fragments" was being read as "asked
  # wrongly" in a case where it meant "nothing left".
  #
  def test_an_emptied_stack_loses_its_sysroot
    with_fake_tc do
      reset_pkgmgr!
      pkg = stack_pkg
      pkgmgr.register(pkg)
      install_into(pkg, OTHER)
      pkgmgr.compose_stack_sysroot(Ver(OTHER))

      root = pkgmgr.stack_sysroot(Ver(OTHER))
      assert File.exist?(root / "usr" / "include" / "stdio.h")

      # Uninstall it for real, the way --clean does.
      pkgmgr.with_host_stack(Ver(OTHER)) do
        FileUtils.rm_rf(pkg.coords.pkgs_dir)
      end
      pkgmgr.refresh()

      assert_equal 0, pkgmgr.compose_stack_sysroot(Ver(OTHER))
      refute File.exist?(root / "usr" / "include" / "stdio.h"),
             "the sysroot still points at an uninstalled package"
    end
  end
end

# The programs, not only the libraries: the sysroot is the stack's
# merged prefix, and usr/bin holds the QEMU built in that stack, the
# gcc that names it and the binutils it builds through.
class TestSysrootHoldsThePrograms < Minitest::Test

  include TestHelper

  STACK = "13.4.0"

  def setup
    reset_pkgmgr!
  end

  # A QEMU install in one stack, at its own prefix, with a binary.
  def qemu_install(pkg, ver)
    pkgmgr.with_host_stack(Ver(STACK)) do
      bin = pkg.coords(Ver(ver)).pkgs_dir / "qemu" / ver / "install" / "bin"
      FileUtils.mkdir_p(bin)
      pkg.expected_files.each { |f, _| FileUtils.touch(bin.parent.parent / f) }
      File.write(bin / "qemu-system-i386", ver)
      pkgmgr.installs_changed!
      pkgmgr.refresh()
    end
  end

  def test_qemu_is_grafted_whole_at_usr
    with_fake_tc do
      q = HostQemuPackage.new
      pkgmgr.register(q)
      qemu_install(q, "9.2.0")

      frags = q.sysroot_fragments(Ver(STACK))
      assert_equal 1, frags.length
      dir, at = frags.first
      assert_equal "usr", at
      assert dir.to_s.end_with?("/qemu/9.2.0/install"), dir.to_s
      assert_empty q.sysroot_fragments(Ver("9.9.9")),
                   "a fragment for a stack it is not in"
    end
  end

  def test_two_qemus_in_one_stack_publish_the_newest
    with_fake_tc do
      q = HostQemuPackage.new
      pkgmgr.register(q)
      qemu_install(q, "6.1.0")
      qemu_install(q, "6.2.0")

      dir, = q.sysroot_fragments(Ver(STACK)).first
      assert dir.to_s.include?("/6.2.0/"), "usr/bin means the newest"
    end
  end

  def test_gcc_and_binutils_binaries_are_grafted_at_usr_bin
    with_fake_tc do
      gcc = HostGccPackage.new
      bu = HostBinutilsPackage.new
      [gcc, bu].each { |p| pkgmgr.register(p) }
      fake_install(gcc, Ver(STACK))
      fake_install(bu)

      gf = gcc.sysroot_fragments(Ver(STACK))
      assert_includes gf.map(&:last), "usr/bin"
      assert gf.any? { |d, at| at == "usr/bin" && d.to_s.end_with?("/bin") }

      bf = bu.sysroot_fragments(Ver(STACK))
      assert_equal ["usr/bin"], bf.map(&:last)
    end
  end

  def test_the_composed_sysroot_has_the_programs
    with_fake_tc do
      q = HostQemuPackage.new
      bu = HostBinutilsPackage.new
      [q, bu].each { |p| pkgmgr.register(p) }
      qemu_install(q, "9.2.0")
      fake_install(bu)

      pkgmgr.compose_stack_sysroot(Ver(STACK))
      bin = pkgmgr.stack_sysroot(Ver(STACK)) / "usr" / "bin"
      assert (bin / "qemu-system-i386").symlink?, "no qemu in usr/bin"
      assert (bin / "ld").symlink?, "no ld in usr/bin"
    end
  end
end
