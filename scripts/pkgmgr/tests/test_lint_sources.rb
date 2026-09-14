# SPDX-License-Identifier: BSD-2-Clause
#
# EVERY SOURCE IS PINNED, AND EVERY PIN IS A SOURCE.
#
# other/pkg_hashes is a table of cache names, and a cache name is
# whatever a SourceRef spells for a version -- the registry is the
# only thing that knows the names, so the registry is what the table
# is checked against:
#
#   * every file the registry can put in the cache on this host has a
#     line, of the kind its fetch mode calls for: a commit for a
#     source we clone and pack, a digest for one downloaded as it is;
#   * every line names a file some host's registry produces -- this
#     host's, or one of the others the tree builds on, whose prebuilt
#     compilers and python carry the host in their names;
#   * no two sources spell one name (PackageManager#validate_sources,
#     which the tool runs at every start and this runs on the real
#     set).
#
# A new package, or a new version of one, fails here until its line
# is added; the tool prints the line to add the first time it fetches
# the file (Cache).
#
require_relative 'test_helper'

class TestLintSources < Minitest::Test
  include TestHelper

  # The hosts the tree builds on, as the constants that name one.
  HOSTS = [
    ["Linux",   "linux",   "x86_64"],
    ["Linux",   "linux",   "aarch64"],
    ["FreeBSD", "freebsd", "x86_64"],
    ["Darwin",  "macos",   "aarch64"],
  ].freeze

  # The registry's cache names as seen from `host`, with who produces
  # each: {name => [[pkg, ver, source], ...]}.
  def names_on(os, host_os, arch)
    with_context(OS: os, HOST_OS: host_os, HOST_ARCH: ALL_ARCHS[arch],
                 HOST_OS_ARCH: "#{host_os}-#{arch}") do
      return pkgmgr.source_files
    end
  end

  # The real registry, with the cross compilers at the versions the
  # configuration names (main.rb reads them at start; the suite does
  # not), put back after.
  def with_real
    saved = ALL_ARCHS.map { |name, arch| [name, arch.gcc_ver] }
    Main.read_gcc_ver_defaults
    with_real_registry { yield }
  ensure
    saved.each { |name, ver| ALL_ARCHS[name].gcc_ver = ver }
  end

  def setup
    reset_pkgmgr!
  end

  def fake(name, source)
    pkgmgr.register(FakePackage.new(name, source: source))
  end

  def test_every_source_on_this_host_has_a_pin_of_its_kind
    with_real do
      pins = pkgmgr.pins
      missing = []
      wrong = []
      for name, users in pkgmgr.source_files.sort do
        pkg, ver, src = users.first
        kind = src.fetch_via_git? && name == src.tarname(ver) ? :git : :sha256
        pin = pins[name]
        if pin.nil?
          missing << "#{name} (#{pkg}:#{ver})"
        elsif pin.kind != kind
          wrong << "#{name}: pinned #{pin.kind}, fetched as #{kind}"
        end
      end
      assert_empty missing, "sources with no line in other/pkg_hashes:\n  " +
                            missing.join("\n  ")
      assert_empty wrong, "pins of the wrong kind:\n  " + wrong.join("\n  ")
    end
  end

  def test_every_pin_names_a_file_some_host_s_registry_produces
    with_real do
      known = HOSTS.flat_map { |h| names_on(*h).keys }.to_set
      # The bootstrap Ruby is fetched by bash before any registry
      # exists; its files are pinned in the same table.
      orphans = pkgmgr.pins.keys.reject { |n|
        known.include?(n) || n.start_with?("ruby-")
      }
      assert_empty orphans, "lines in other/pkg_hashes that name no " \
                            "source:\n  " + orphans.join("\n  ")
    end
  end

  def test_the_real_registry_s_names_are_one_file_each
    with_real { pkgmgr.validate_sources }
  end

  def test_a_name_two_sources_spell_is_refused_at_load
    with_fake_tc do
      fake("aaa", SourceRef.new(name: "same", url: "https://x/a"))
      fake("bbb", SourceRef.new(name: "same", url: "https://x/b"))
      e = assert_raises(PackageManager::SourceNameError) {
        pkgmgr.validate_sources
      }
      assert_match(/one cache slot: same-1.0.0.tgz from aaa:1.0.0, /, e.message)
      assert_match(/same-1.0.0.tgz from bbb:1.0.0/, e.message)
    end
  end

  def test_two_spellings_a_case_insensitive_filesystem_folds_are_refused
    with_fake_tc do
      fake("aaa", SourceRef.new(name: "libX", url: "https://x/a"))
      fake("bbb", SourceRef.new(name: "libx", url: "https://x/b"))
      e = assert_raises(PackageManager::SourceNameError) {
        pkgmgr.validate_sources
      }
      assert_match(/libX-1.0.0.tgz from aaa/, e.message)
      assert_match(/libx-1.0.0.tgz from bbb/, e.message)
    end
  end

  def test_one_source_two_packages_is_sharing_not_a_collision
    with_fake_tc do
      shared = SourceRef.new(name: "shared", url: "https://x/s")
      fake("aaa", shared)
      fake("bbb", shared)
      pkgmgr.validate_sources
    end
  end

  def test_a_name_a_table_cannot_hold_is_refused_at_load
    with_fake_tc do
      fake("aaa", SourceRef.new(name: "odd", url: "https://x/a",
                                tarname: ->(v) { "odd v#{v}.tgz" }))
      e = assert_raises(PackageManager::SourceNameError) {
        pkgmgr.validate_sources
      }
      assert_match(/odd v1.0.0.tgz \(aaa\): not a cache name/, e.message)
    end
  end
end
