# SPDX-License-Identifier: BSD-2-Clause
#
# THE HOST'S LIBRARIES AS A BUILD INPUT, and the env of a rolling
# distro.
#
# A :distro or :compiler tier install links the host's shared
# libraries. On a fixed-release distro the release names the set
# (ubuntu-22.04); on a rolling one nothing does: Arch says
# BUILD_ID=rolling and has no VERSION_ID, and Omarchy's VERSION_ID
# versions its desktop layer while glibc moves on Arch's schedule. So
# the env of a rolling distro is its ID alone, and what an install was
# built against is recorded, file by file with its digest, in
# .build_inputs; an install whose libraries moved under it reads as
# changed, --check-for-updates lists it, --rebuild builds it again.
#

require_relative 'test_helper'
require_relative '../system_libs'

class TestRollingDistroEnv < Minitest::Test

  def slug(data)
    InitOnly.stub(:parse_os_release, data) { InitOnly.get_host_distro("linux") }
  end

  def test_a_fixed_release_distro_is_id_and_version
    assert_equal "ubuntu-22.04",
                 slug({ "ID" => "ubuntu", "VERSION_ID" => "22.04" })
    assert_equal "fedora-41", slug({ "ID" => "fedora", "VERSION_ID" => "41" })
  end

  # Arch on bare metal: no VERSION_ID at all. It used to be refused.
  def test_arch_is_its_id
    assert_equal "arch", slug({ "ID" => "arch", "BUILD_ID" => "rolling" })
  end

  # Arch's container image writes the image's build date as the
  # version; a date is not a library set.
  def test_the_arch_container_is_arch_too
    assert_equal "arch", slug({ "ID" => "arch", "BUILD_ID" => "rolling",
                                "VERSION_ID" => "20260830.0.582275" })
  end

  # A derivative versions its own layer, not the libraries.
  def test_an_arch_derivative_is_its_id
    assert_equal "omarchy", slug({ "ID" => "omarchy", "ID_LIKE" => "arch",
                                   "VERSION_ID" => "4.0.4",
                                   "BUILD_ID" => "4.0.4" })
    assert_equal "endeavouros", slug({ "ID" => "endeavouros",
                                       "ID_LIKE" => "arch",
                                       "VERSION_ID" => "2026" })
  end

  def test_any_distro_saying_rolling_is_its_id
    assert_equal "opensuse-tumbleweed",
                 slug({ "ID" => "opensuse-tumbleweed",
                        "VERSION_ID" => "20260915", "BUILD_ID" => "rolling" })
  end

  def test_a_distro_with_no_version_and_no_rolling_claim_is_its_id
    assert_equal "gentoo", slug({ "ID" => "gentoo" })
  end

  def test_no_id_is_refused
    out, err = capture_io {
      assert_raises(SystemExit) { slug({ "VERSION_ID" => "1" }) }
    }
    assert_match(/missing ID/, out + err)
  end
end

class TestSystemLibsRecorded < Minitest::Test

  include TestHelper

  def setup = reset_pkgmgr!

  # The reader resolves what a binary links outside the toolchain,
  # symlinks followed, with a digest of each. This machine's own
  # /bin/true is the dynamic binary at hand.
  def test_the_reader_resolves_a_binary_to_the_hosts_files
    skip "no ldd here" if `which ldd 2>/dev/null`.empty?
    Dir.mktmpdir do |d|
      FileUtils.cp("/bin/true", "#{d}/bin_true")
      libs = SystemLibs.of_install(Pathname(d), tc: Pathname("/nonexistent"))
      assert libs.keys.any? { |p| p.include?("libc.so") }, libs.keys.inspect
      assert libs.keys.all? { |p| p.start_with?("/") && !File.symlink?(p) },
             "not resolved to real files: #{libs.keys.inspect}"
      assert libs.values.all? { |v| v.start_with?("sha256:") }
      assert libs.keys.any? { |p| p.include?("ld-linux") },
             "the loader itself is one"
    end
  end

  def test_a_text_file_or_a_static_tree_contributes_nothing
    Dir.mktmpdir do |d|
      File.write("#{d}/script", "#!/bin/sh\n")
      assert_equal({}, SystemLibs.of_install(Pathname(d)))
    end
  end

  # What resolves INSIDE the toolchain is the tree's own to record;
  # only the host's files are system libraries. A binary whose every
  # library is under `tc` records nothing.
  def test_libraries_inside_the_toolchain_are_not_system_libraries
    skip "no ldd here" if `which ldd 2>/dev/null`.empty?
    Dir.mktmpdir do |d|
      FileUtils.cp("/bin/true", "#{d}/bin_true")
      all = SystemLibs.of_install(Pathname(d), tc: Pathname("/nonexistent"))
      refute_empty all
      # Everything /bin/true links lives under /usr (or /lib -> /usr/lib
      # on merged-usr systems): with that as the toolchain, nothing is
      # the host's.
      assert_equal({}, SystemLibs.of_install(Pathname(d), tc: Pathname("/usr")))
    end
  end

  # Only the tiers that link the host record anything: a :stack
  # package's binaries resolve into the toolchain, a target's into
  # nothing of the host's.
  def test_only_the_tiers_that_link_the_host_record_libraries
    for tier, links in { distro: true, compiler: true, portable: false,
                         stack: false } do
      p = FakePackage.new("host_t", on_host: true, host_tier: tier,
                          arch_list: ALL_HOST_ARCHS.values)
      assert_equal links, p.links_the_host?, tier.to_s
    end
    refute FakePackage.new("t").links_the_host?
  end

  # The record round-trips, and the judgement reads it: a library
  # whose digest moved, or that is gone, makes the install :changed;
  # one that did not leaves it :ok. Rewriting the record in the
  # current spelling keeps the lines.
  def test_a_moved_host_library_makes_the_install_changed
    with_fake_tc do
      with_stubbed_externals do
        Dir.mktmpdir do |host|
          lib = Pathname(host) / "libfake.so.1"
          File.write(lib, "v1")
          h = FakePackage.new("host_h", on_host: true, host_tier: :distro)
          pkgmgr.register(h)
          pkgmgr.install("host_h")
          inst = bound(h).find_install(Ver("1.0.0"))
          assert_equal :ok, bound(h).build_inputs_state_of(inst)

          # As if the build had linked it: the record names the file.
          BuildInputs.write(inst.path,
                            recipe: bound(h).build_recipe_digest(Ver("1.0.0")),
                            files: [],
                            syslibs: { lib.to_s => SystemLibs.digest(lib) })
          assert_equal({ lib.to_s => SystemLibs.digest(lib) },
                       BuildInputs.syslibs_of(inst.path))
          assert_equal :ok, bound(h).build_inputs_state_of(inst)
          assert_equal({}, BuildInputs.syslibs_changed(inst.path))

          File.write(lib, "v2")
          assert_equal({ lib.to_s => "changed" },
                       BuildInputs.syslibs_changed(inst.path))
          assert_equal :changed, bound(h).build_inputs_state_of(inst)

          File.delete(lib)
          assert_equal({ lib.to_s => "missing" },
                       BuildInputs.syslibs_changed(inst.path))
          assert_equal :changed, bound(h).build_inputs_state_of(inst)

          # A record with no syslib lines (a package of another tier,
          # or one from before) judges the sources alone.
          BuildInputs.write(inst.path,
                            recipe: bound(h).build_recipe_digest(Ver("1.0.0")),
                            files: [])
          assert_equal :ok, bound(h).build_inputs_state_of(inst)
          assert_equal({}, BuildInputs.syslibs_of(inst.path))

          # ...and no record at all names no libraries.
          File.delete(inst.path / BuildInputs::FILE)
          assert_equal({}, BuildInputs.syslibs_of(inst.path))
          assert_equal({}, BuildInputs.syslibs_changed(inst.path))
        end
      end
    end
  end

  # --check-for-updates and -l say it, and --rebuild takes it.
  def test_the_tree_reports_and_rebuilds_a_changed_host_library
    with_fake_tc do
      with_stubbed_externals do
        Dir.mktmpdir do |host|
          lib = Pathname(host) / "libfake.so.1"
          File.write(lib, "v1")
          h = FakePackage.new("host_h", on_host: true, host_tier: :distro)
          pkgmgr.register(h)
          run_cli("-s", "host_h", "-q")
          inst = bound(h).find_install(Ver("1.0.0"))
          BuildInputs.write(inst.path,
                            recipe: bound(h).build_recipe_digest(Ver("1.0.0")),
                            files: [],
                            syslibs: { lib.to_s => SystemLibs.digest(lib) })
          pkgmgr.installs_changed!

          rc, _ = run_cli("--check-for-updates", "-q")
          assert_equal 0, rc, "nothing moved yet"

          File.write(lib, "v2")
          rc, out = run_cli("--check-for-updates", "-q")
          assert_equal 2, rc
          assert_match(/NEEDS_REBUILD host_h/, out)

          rc, out = run_cli("-l", "-q")
          assert_equal 0, rc
          assert_match(/Changed under them/, out)
          assert_match(/host_h 1.0.0\s+libfake.so.1 \(changed\)/, out)

          FakePackage.clear_log!
          rc, out = run_cli("--rebuild", "-q")
          assert_equal 0, rc, out
          assert_includes FakePackage.install_log, "host_h"
          rc, _ = run_cli("--check-for-updates", "-q")
          assert_equal 0, rc, "rebuilt against what is there now"
        end
      end
    end
  end
end
