# SPDX-License-Identifier: BSD-2-Clause

require_relative 'test_helper'
require 'stringio'

class TestShowStatus < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # Capture stdout from a block and return it as a string.
  def capture_stdout(&block)
    old = $stdout
    $stdout = StringIO.new
    block.call
    $stdout.string
  ensure
    $stdout = old
  end

  # --- show_status (individual package line) ---

  def test_show_status_empty
    output = capture_stdout {
      pkgmgr.show_status("foo", nil, [])
    }
    assert_match(/foo/, output)
    # Empty list → empty status
  end

  def test_show_status_installed_single_arch
    with_fake_tc do
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")
        pkgmgr.refresh()

        list = pkg.get_install_list
        output = capture_stdout {
          pkgmgr.show_status("foo", nil, list)
        }
        assert_match(/foo/, output)
        assert_match(/installed/, output)
        assert_match(/#{ARCH.name}/, output)
      end
    end
  end

  def test_show_status_installed_multiple_archs
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")

        # A real second install, not a bare directory: an install
        # carries a .build_inputs record, and one without a record is
        # a package needing a rebuild, not a healthy second arch.
        with_context(ARCH: ALL_ARCHS["x86_64"], BOARD: nil) do
          pkgmgr.install("foo")
        end
        pkgmgr.refresh()

        list = pkg.get_install_list
        output = capture_stdout {
          pkgmgr.show_status("foo", nil, list)
        }
        assert_match(/installed/, output)
        assert_match(/i386/, output)
        assert_match(/x86_64/, output)
      end
    end
  end

  def test_show_status_broken_only
    with_fake_tc do |tc|
      # Create a version dir with no expected files (broken)
      gcc = FAKE_GCC_VER.to_s
      FileUtils.mkdir_p(target_pkgs(ARCH, gcc) / "brkpkg" / "1.0.0")

      # Register a package that expects a file
      pkg = FakePackage.new("brkpkg")
      # Override expected_files to require something that doesn't exist
      pkg.define_singleton_method(:expected_files) { |ver = nil|
        [["nonexistent_binary", false]]
      }
      pkgmgr.register(pkg)
      pkgmgr.refresh()

      list = pkg.get_install_list
      assert list.any? { |x| x.broken }

      output = capture_stdout {
        pkgmgr.show_status("brkpkg", nil, list)
      }
      assert_match(/broken/, output)
      # Broken install should NOT show the arch
      refute_match(/i386/, output)
    end
  end

  # Every arch that has an install appears in the line, whichever arch
  # the invocation is for. (Broken installs have their own test above;
  # this one is about the arch list.)
  def test_arch_list_covers_every_installed_arch
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")

        with_context(ARCH: ALL_ARCHS["riscv64"], BOARD: nil) do
          pkgmgr.install("foo")
        end
        pkgmgr.refresh()

        list = pkg.get_install_list
        output = capture_stdout {
          pkgmgr.show_status("foo", nil, list)
        }
        assert_match(/installed/, output)
        assert_match(/i386/, output)
        assert_match(/riscv64/, output)
      end
    end
  end

  # An install is judged against the recipe as it reads at ITS OWN
  # coordinates. Asking the package about a version instead re-derives
  # the CURRENT ones, finds nothing where it looked, and reports the
  # install as healthy -- so a stale x86_64 tree was drawn "installed"
  # from an i386 invocation, while --check-for-updates, which does use
  # each install's coordinates, called the same tree stale.
  def test_a_stale_install_of_another_arch_is_still_stale
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")

        with_context(ARCH: ALL_ARCHS["x86_64"], BOARD: nil) do
          pkgmgr.install("foo")
        end
        pkgmgr.refresh()

        # Spoil only the x86_64 record. The i386 one, which is what
        # the current coordinates point at, stays correct.
        other = pkg.get_install_list.find { |i|
          i.arch == ALL_ARCHS["x86_64"]
        }
        refute_nil other, "no x86_64 install to spoil"
        File.write(other.path / BuildInputs::FILE, "recipe sha256:spoiled\n")

        output = capture_stdout {
          pkgmgr.show_status("foo", nil, pkg.get_install_list)
        }
        assert_match(/stale/, output)
      end
    end
  end

  def test_show_status_noarch_package
    with_fake_tc do |tc|
      dir = noarch_pkgs / "noarch_foo" / "1.0.0"
      FileUtils.mkdir_p(dir)

      pkg = FakePackage.new("noarch_foo", arch_list: nil)
      pkgmgr.register(pkg)

      # A directory alone is not an install: without a record of what
      # it was built from, pkgmgr cannot say it matches the sources.
      # This test is about the noarch LABEL, so give it one.
      BuildInputs.write(dir, recipe: pkg.build_recipe_digest(Ver("1.0.0")),
                        files: pkg.build_files(Ver("1.0.0")))
      pkgmgr.refresh()

      list = pkg.get_install_list
      output = capture_stdout {
        pkgmgr.show_status("noarch_foo", nil, list)
      }
      assert_match(/installed/, output)
      assert_match(/noarch/, output)
    end
  end

  # ...and without one, it says so rather than claiming the install is
  # good. A directory somebody created by hand is exactly the case.
  def test_an_install_with_no_record_shows_stale
    with_fake_tc do |tc|
      FileUtils.mkdir_p(noarch_pkgs / "handmade" / "1.0.0")
      pkg = FakePackage.new("handmade", arch_list: nil)
      pkgmgr.register(pkg)
      pkgmgr.refresh()

      output = capture_stdout {
        pkgmgr.show_status("handmade", nil, pkg.get_install_list)
      }
      assert_match(/stale/, output)
    end
  end

  def test_show_status_host_package
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("host_foo", on_host: true,
                              arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(pkg)
        pkgmgr.install("host_foo")
        pkgmgr.refresh()

        list = pkg.get_install_list
        output = capture_stdout {
          pkgmgr.show_status("host_foo", nil, list)
        }
        assert_match(/installed/, output)
        assert_match(/host/, output)
      end
    end
  end

  def test_show_status_group_by_arch
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")
        pkgmgr.refresh()

        list = pkg.get_install_list
        output = capture_stdout {
          pkgmgr.show_status("foo", "arch", list)
        }
        assert_match(/i386/, output)
        assert_match(/1\.0\.0/, output)
      end
    end
  end

  def test_show_status_group_by_ver
    with_fake_tc do |tc|
      with_stubbed_externals do
        pkg = FakePackage.new("foo")
        pkgmgr.register(pkg)
        pkgmgr.install("foo")
        pkgmgr.refresh()

        list = pkg.get_install_list
        output = capture_stdout {
          pkgmgr.show_status("foo", "ver", list)
        }
        assert_match(/1\.0\.0/, output)
        assert_match(/i386/, output)
      end
    end
  end

  def test_show_status_found_not_registered
    # An install that's found on disk but not from a registered package
    info = InstallInfo.new(
      "orphan_pkg", Ver("13.3.0"), false, ARCH, Ver("1.0.0"),
      Pathname.new("/fake/orphan"), nil, false,
      coords: Coords.new("tilck-#{ARCH.name}", ARCH.default_board,
                         "gcc-13.3.0")
    )
    output = capture_stdout {
      pkgmgr.show_status("orphan_pkg", nil, [info])
    }
    assert_match(/found/, output)
  end
end

class TestShowStatusAll < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  def capture_stdout(&block)
    old = $stdout
    $stdout = StringIO.new
    block.call
    $stdout.string
  ensure
    $stdout = old
  end

  def test_show_all_with_installed_packages
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.register(FakePackage.new("bar"))
        pkgmgr.install("foo")
        pkgmgr.refresh()

        output = capture_stdout { pkgmgr.show_status_all }
        assert_match(/foo/, output)
        assert_match(/bar/, output)
        assert_match(/installed/, output)
      end
    end
  end

  def test_show_all_groups_by_type
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("target_pkg"))
        pkgmgr.register(FakePackage.new("host_tool", on_host: true,
                                        arch_list: ALL_HOST_ARCHS.values))
        pkgmgr.register(FakePackage.new("noarch_pkg", arch_list: nil))
        pkgmgr.install("target_pkg")
        pkgmgr.install("host_tool")
        pkgmgr.refresh()

        output = capture_stdout { pkgmgr.show_status_all }
        assert_match(/Packages built by system CC/, output)
        assert_match(/Source-only packages/, output)
        assert_match(/Packages built by GCC/, output)
      end
    end
  end

  def test_show_all_with_compiler
    with_fake_tc do
      with_stubbed_externals do
        cc = FakePackage.new("gcc-#{ARCH.name}-musl",
                             on_host: true, is_compiler: true,
                             arch_list: ALL_HOST_ARCHS.values)
        pkgmgr.register(cc)
        pkgmgr.install("gcc-#{ARCH.name}-musl")
        pkgmgr.refresh()

        output = capture_stdout { pkgmgr.show_status_all }
        assert_match(/GCC toolchains/, output)
        assert_match(/gcc-#{ARCH.name}-musl/, output)
      end
    end
  end

  def test_show_all_group_by_arch
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        pkgmgr.refresh()

        output = capture_stdout { pkgmgr.show_status_all("arch") }
        assert_match(/i386/, output)
      end
    end
  end

  def test_show_all_group_by_ver
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("foo"))
        pkgmgr.install("foo")
        pkgmgr.refresh()

        output = capture_stdout { pkgmgr.show_status_all("ver") }
        assert_match(/1\.0\.0/, output)
      end
    end
  end

  def test_show_all_not_installed_shows_no_status
    with_fake_tc do
      with_stubbed_externals do
        pkgmgr.register(FakePackage.new("uninstalled_pkg"))
        pkgmgr.refresh()

        output = capture_stdout { pkgmgr.show_status_all }
        assert_match(/uninstalled_pkg/, output)
        # Should NOT show as installed
        refute_match(/installed.*uninstalled_pkg/, output)
      end
    end
  end
end
