# SPDX-License-Identifier: BSD-2-Clause
#
# The hermeticity audit. check_refs is the whole judgement and takes no
# filesystem, so most of this is pure.
#

require_relative 'test_helper'
require_relative '../hermeticity'

class TestHermeticityJudgement < Minitest::Test

  ALLOWED = ["/tc"].freeze

  def check(interp: nil, rpaths: [], resolved: {})
    return Hermeticity.check_refs("/tc/bin/x", interp: interp,
                                  rpaths: rpaths, resolved: resolved,
                                  allowed: ALLOWED)
  end

  def test_a_clean_binary_has_no_violations
    assert_empty check(interp: "/tc/lib/ld.so",
                       rpaths: ["/tc/lib"],
                       resolved: { "libc.so.6" => "/tc/lib/libc.so.6" })
  end

  # The single most important check: an executable pointed at the
  # system loader will load the system libc no matter what else is
  # right about it.
  def test_system_interpreter_is_a_violation
    v = check(interp: "/lib64/ld-linux-x86-64.so.2")
    assert_equal 1, v.length
    assert_equal "interpreter", v.first.kind
    assert_match(%r{/lib64/ld-linux}, v.first.detail)
  end

  def test_system_rpath_is_a_violation
    v = check(interp: "/tc/lib/ld.so", rpaths: ["/tc/lib", "/usr/lib"])
    assert_equal 1, v.length
    assert_equal "rpath", v.first.kind
    assert_equal "/usr/lib", v.first.detail
  end

  # RPATH and interpreter can both look right while a library still
  # comes from the system, which is why resolution is checked too.
  def test_library_resolving_outside_is_a_violation
    v = check(interp: "/tc/lib/ld.so",
              resolved: { "libz.so.1" => "/usr/lib/libz.so.1" })
    assert_equal 1, v.length
    assert_equal "resolved outside", v.first.kind
    assert_match(/libz\.so\.1/, v.first.detail)
    assert_match(%r{/usr/lib}, v.first.detail)
  end

  def test_unresolved_library_is_a_violation
    v = check(interp: "/tc/lib/ld.so", resolved: { "libfoo.so" => nil })
    assert_equal 1, v.length
    assert_equal "unresolved", v.first.kind
  end

  # $ORIGIN is relative to the file being loaded, which is itself
  # inside the toolchain — and making the tree relocatable is the
  # reason to use it.
  def test_origin_relative_rpath_is_fine
    assert_empty check(interp: "/tc/lib/ld.so",
                       rpaths: ["$ORIGIN/../lib"])
  end

  # The kernel maps the vDSO into every process; it has no path to be
  # outside of.
  def test_vdso_is_not_a_violation
    assert_empty check(interp: "/tc/lib/ld.so",
                       resolved: { Hermeticity::VDSO => nil })
  end

  def test_shared_library_has_no_interpreter_and_that_is_fine
    assert_empty check(interp: nil, rpaths: ["/tc/lib"])
  end

  def test_every_violation_is_reported_not_just_the_first
    v = check(interp: "/lib64/ld.so",
              rpaths: ["/usr/lib", "/opt/lib"],
              resolved: { "libz.so.1" => "/usr/lib/libz.so.1" })
    assert_equal 4, v.length
    assert_equal ["interpreter", "resolved outside", "rpath", "rpath"],
                 v.map(&:kind).sort
  end

  def test_a_prefix_that_only_looks_like_the_root_does_not_pass
    # "/tcx" must not be accepted just because it starts with "/tc".
    v = check(interp: "/tcx/lib/ld.so")
    assert_equal 1, v.length
  end

  def test_violation_renders_readably
    v = check(interp: "/lib64/ld.so").first
    assert_match(%r{/tc/bin/x: interpreter: /lib64/ld\.so}, v.to_s)
  end
end

class TestHermeticityElfDetection < Minitest::Test

  def test_elf_magic_is_recognised
    Dir.mktmpdir do |dir|
      f = File.join(dir, "bin")
      File.binwrite(f, "\x7fELF\x02\x01\x01\x00")
      assert Hermeticity.elf?(f)
    end
  end

  def test_a_text_file_is_not_elf
    Dir.mktmpdir do |dir|
      f = File.join(dir, "script")
      File.write(f, "#!/bin/sh\necho hi\n")
      refute Hermeticity.elf?(f)
    end
  end

  def test_a_directory_is_not_elf
    Dir.mktmpdir { |dir| refute Hermeticity.elf?(dir) }
  end

  def test_an_empty_file_is_not_elf
    Dir.mktmpdir do |dir|
      f = File.join(dir, "empty")
      File.write(f, "")
      refute Hermeticity.elf?(f)
    end
  end

  # The sysroot is a symlink farm; following every link would audit the
  # same file once per stack that references it.
  def test_symlinks_are_skipped
    Dir.mktmpdir do |dir|
      real = File.join(dir, "real")
      File.binwrite(real, "\x7fELF\x02\x01\x01\x00")
      link = File.join(dir, "link")
      File.symlink(real, link)
      refute Hermeticity.elf?(link)
    end
  end
end

#
# Parsing the loader's --list output. The first version of this read
# the loader's own line — a bare absolute path with a load address and
# no "=>" — as an unresolved dependency, and reported every one of
# glibc's ~250 gconv modules as a violation.
#
class TestHermeticityLoaderOutput < Minitest::Test

  def parse(text)
    fake = File.join(Dir.tmpdir, "fake-loader-#{$$}")
    File.write(fake, "#!/bin/sh\ncat <<'EOT'\n#{text}\nEOT\n")
    File.chmod(0755, fake)
    begin
      return Hermeticity.resolve_libs("/tc/bin/x", loader: fake)
    ensure
      File.unlink(fake)
    end
  end

  def test_resolved_library_is_captured
    got = parse("\tlibc.so.6 => /tc/lib/libc.so.6 (0x00007f0000000000)")
    assert_equal({ "libc.so.6" => "/tc/lib/libc.so.6" }, got)
  end

  def test_the_loaders_own_line_is_resolved_not_missing
    got = parse("\t/tc/lib/ld-linux-x86-64.so.2 (0x00007f0000000000)")
    assert_equal({ "/tc/lib/ld-linux-x86-64.so.2" =>
                   "/tc/lib/ld-linux-x86-64.so.2" }, got)

    # ...and so it is not a violation.
    assert_empty Hermeticity.check_refs("/tc/bin/x", interp: nil,
                                        rpaths: [], resolved: got,
                                        allowed: ["/tc"])
  end

  def test_not_found_is_unresolved
    got = parse("\tlibmissing.so.1 => not found")
    assert_equal({ "libmissing.so.1" => nil }, got)
  end

  def test_vdso_is_dropped
    assert_empty parse("\tlinux-vdso.so.1 (0x00007ffd00000000)")
  end

  def test_empty_output_means_nothing_to_check
    assert_nil parse("")
  end

  def test_a_realistic_mixed_listing
    got = parse([
      "\tlinux-vdso.so.1 (0x00007ffd1234)",
      "\tlibc.so.6 => /tc/lib/libc.so.6 (0x00007f1111)",
      "\tlibz.so.1 => /usr/lib/libz.so.1 (0x00007f2222)",
      "\t/tc/lib/ld-linux-x86-64.so.2 (0x00007f3333)",
    ].join("\n"))

    assert_equal 3, got.length
    refute got.key?(Hermeticity::VDSO)

    v = Hermeticity.check_refs("/tc/bin/x", interp: nil, rpaths: [],
                               resolved: got, allowed: ["/tc"])
    assert_equal 1, v.length
    assert_match(/libz\.so\.1/, v.first.detail)
  end
end

#
# The hostile-environment check. Resolving correctly in a CLEAN
# environment proves very little: our loader has the sysroot compiled
# in as its default search path, so a binary carrying no RPATH resolves
# correctly when nothing competes and silently loads system libraries
# when something does. The audit therefore asks the hostile question.
#
class TestHermeticityHostileCheck < Minitest::Test

  def test_system_libdirs_are_declared
    refute_empty Hermeticity::SYSTEM_LIBDIRS
    assert_includes Hermeticity::SYSTEM_LIBDIRS, "/usr/lib"
  end

  # What the audit missed before: a binary with no RPATH that resolves
  # correctly only because nothing was competing.
  def test_a_binary_relying_on_loader_defaults_is_caught
    clean = { "libc.so.6" => "/tc/lib/libc.so.6" }
    hostile = { "libc.so.6" => "/usr/lib/libc.so.6" }

    assert_empty Hermeticity.check_refs("/tc/bin/x", interp: "/tc/lib/ld.so",
                                        rpaths: [], resolved: clean,
                                        allowed: ["/tc"])

    v = Hermeticity.check_refs("/tc/bin/x", interp: "/tc/lib/ld.so",
                               rpaths: [], resolved: hostile,
                               allowed: ["/tc"])
    assert_equal 1, v.length
    assert_equal "resolved outside", v.first.kind
  end
end
