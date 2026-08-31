# SPDX-License-Identifier: BSD-2-Clause
#
# The portability audit. check_refs is the whole judgement and takes no
# filesystem, so most of this is pure.
#

require_relative 'test_helper'
require_relative '../portability'

class TestPortabilityJudgement < Minitest::Test

  ALLOWED = ["/tc"].freeze

  def check(interp: nil, rpaths: [], resolved: {})
    return Portability.check_refs("/tc/bin/x", interp: interp,
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
    assert_equal "non-portable reference", v.first.kind
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
                       resolved: { Portability::VDSO => nil })
  end

  def test_shared_library_has_no_interpreter_and_that_is_fine
    assert_empty check(interp: nil, rpaths: ["/tc/lib"])
  end

  def test_every_violation_is_reported_not_just_the_first
    v = check(interp: "/lib64/ld.so",
              rpaths: ["/usr/lib", "/opt/lib"],
              resolved: { "libz.so.1" => "/usr/lib/libz.so.1" })
    assert_equal 4, v.length
    assert_equal ["interpreter", "non-portable reference",
                  "rpath", "rpath"],
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

class TestPortabilityElfDetection < Minitest::Test

  # elf? means "an ELF this host could execute", so the magic alone is
  # not enough: it also has to be 64-bit little-endian x86-64. See
  # TestPortabilityForeignElf for why.
  def test_a_native_elf_header_is_recognised
    Dir.mktmpdir do |dir|
      f = File.join(dir, "bin")
      hdr = "\x7fELF".b + [2, 1, 1].pack("C3") + ("\0" * 9) +
            [2].pack("v") + [62].pack("v") + ("\0" * 44)
      File.binwrite(f, hdr)
      assert Portability.elf?(f)
    end
  end

  def test_the_magic_alone_is_not_enough
    Dir.mktmpdir do |dir|
      f = File.join(dir, "stub")
      File.binwrite(f, "\x7fELF\x02\x01\x01\x00")
      refute Portability.elf?(f)
    end
  end

  def test_a_text_file_is_not_elf
    Dir.mktmpdir do |dir|
      f = File.join(dir, "script")
      File.write(f, "#!/bin/sh\necho hi\n")
      refute Portability.elf?(f)
    end
  end

  def test_a_directory_is_not_elf
    Dir.mktmpdir { |dir| refute Portability.elf?(dir) }
  end

  def test_an_empty_file_is_not_elf
    Dir.mktmpdir do |dir|
      f = File.join(dir, "empty")
      File.write(f, "")
      refute Portability.elf?(f)
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
      refute Portability.elf?(link)
    end
  end
end

#
# Parsing the loader's --list output. The first version of this read
# the loader's own line — a bare absolute path with a load address and
# no "=>" — as an unresolved dependency, and reported every one of
# glibc's ~250 gconv modules as a violation.
#
class TestPortabilityLoaderOutput < Minitest::Test

  def parse(text)
    fake = File.join(Dir.tmpdir, "fake-loader-#{$$}")
    File.write(fake, "#!/bin/sh\ncat <<'EOT'\n#{text}\nEOT\n")
    File.chmod(0755, fake)
    begin
      return Portability.resolve_libs("/tc/bin/x", loader: fake)
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
    assert_empty Portability.check_refs("/tc/bin/x", interp: nil,
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
    refute got.key?(Portability::VDSO)

    v = Portability.check_refs("/tc/bin/x", interp: nil, rpaths: [],
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
class TestPortabilityHostileCheck < Minitest::Test

  def test_system_libdirs_are_declared
    refute_empty Portability::SYSTEM_LIBDIRS
    assert_includes Portability::SYSTEM_LIBDIRS, "/usr/lib"
  end

  # What the audit missed before: a binary with no RPATH that resolves
  # correctly only because nothing was competing.
  def test_a_binary_relying_on_loader_defaults_is_caught
    clean = { "libc.so.6" => "/tc/lib/libc.so.6" }
    hostile = { "libc.so.6" => "/usr/lib/libc.so.6" }

    assert_empty Portability.check_refs("/tc/bin/x", interp: "/tc/lib/ld.so",
                                        rpaths: [], resolved: clean,
                                        allowed: ["/tc"])

    v = Portability.check_refs("/tc/bin/x", interp: "/tc/lib/ld.so",
                               rpaths: [], resolved: hostile,
                               allowed: ["/tc"])
    assert_equal 1, v.length
    assert_equal "non-portable reference", v.first.kind
  end
end

#
# Foreign-architecture ELF. QEMU ships guest firmware for other
# machines as ELF data — s390-ccw.img, s390-netboot.img — whose
# interpreters name paths that will never exist on this host
# (/lib/ld64.so.1). The audit asks whether a file would load system
# libraries HERE, which is not a question about them.
#
class TestPortabilityForeignElf < Minitest::Test

  # Minimal 64-bit little-endian ELF header with the given e_machine.
  def elf_header(machine)
    h = "\x7fELF".b + [2, 1, 1].pack("C3") + ("\0" * 9)
    h += [2].pack("v")            # e_type: ET_EXEC
    h += [machine].pack("v")      # e_machine
    return h + ("\0" * 44)
  end

  def write(dir, name, bytes)
    p = File.join(dir, name)
    File.binwrite(p, bytes)
    return p
  end

  def test_native_x86_64_is_audited
    Dir.mktmpdir do |d|
      assert Portability.elf?(write(d, "native", elf_header(62)))
    end
  end

  def test_s390x_firmware_is_skipped
    Dir.mktmpdir do |d|
      # EM_S390 is 22 — the machine of the .img files QEMU installs.
      refute Portability.elf?(write(d, "s390-ccw.img", elf_header(22)))
    end
  end

  def test_aarch64_and_riscv_are_skipped_too
    Dir.mktmpdir do |d|
      refute Portability.elf?(write(d, "arm64", elf_header(183)))
      refute Portability.elf?(write(d, "riscv", elf_header(243)))
    end
  end

  def test_a_truncated_file_is_not_elf
    Dir.mktmpdir do |d|
      refute Portability.elf?(write(d, "short", "\x7fELF".b + "\0\0\0"))
    end
  end

  def test_big_endian_is_not_treated_as_native
    Dir.mktmpdir do |d|
      h = elf_header(62).dup
      h[5] = "\x02"    # EI_DATA: MSB
      refute Portability.elf?(write(d, "be", h))
    end
  end
end
