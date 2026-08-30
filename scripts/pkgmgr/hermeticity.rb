# SPDX-License-Identifier: BSD-2-Clause

#
# The hermeticity audit: proof that what we built references nothing
# outside the toolchain.
#
# "Hermetic" rots silently. One configure script slipping
# -I/usr/include or -L/usr/lib through is enough, nothing fails at the
# time, and the breakage surfaces months later on a machine with a
# different distro. The whole approach — building for the host's own
# triple rather than a distinct one, which is what keeps configure
# scripts able to run their test programs — trades exactly this risk
# for that convenience. It is only a good trade if the risk is
# detected, so this is a deliverable rather than a debugging aid.
#
# Three things are checked per ELF file:
#
#   * the interpreter: an executable whose ELF interpreter is the
#     system loader will load the system libc no matter what else is
#     right;
#   * RPATH / RUNPATH: every entry must be inside the toolchain or
#     $ORIGIN-relative;
#   * resolved libraries: where each DT_NEEDED actually comes from.
#
# The checking is pure and lives in check_refs; reading the ELF files
# is the thin shell around it.
#

require 'pathname'
require 'shellwords'

module Hermeticity

  Violation = Struct.new(:path, :kind, :detail) do
    def to_s = "#{path}: #{kind}: #{detail}"
  end

  # Not a file: the kernel maps the vDSO into every process and it
  # appears in any resolution list. It has no path to be outside of.
  VDSO = "linux-vdso.so.1"

  module_function

  # Does `ref` live under one of `allowed`?
  #
  # $ORIGIN-relative entries are fine by construction: they resolve
  # relative to the file being loaded, which is itself inside the
  # toolchain, and making the tree relocatable is the point of using
  # them.
  def allowed_ref?(ref, allowed)
    return true if ref.nil? || ref.empty?
    return true if ref.start_with?("$ORIGIN")
    return allowed.any? { |root| ref.start_with?(root.to_s + "/") }
  end

  # The whole judgement, with no filesystem involved: given what one
  # ELF file references, say what is wrong with it.
  #
  # interp:   PT_INTERP, or nil for a shared library
  # rpaths:   DT_RPATH and DT_RUNPATH entries, already split on ':'
  # resolved: { soname => resolved path or nil }
  def check_refs(path, interp:, rpaths:, resolved:, allowed:)

    out = []

    if !interp.nil? && !allowed_ref?(interp, allowed)
      out << Violation.new(path, "interpreter", interp)
    end

    for r in rpaths
      next if allowed_ref?(r, allowed)
      out << Violation.new(path, "rpath", r)
    end

    for soname, target in resolved
      next if soname == VDSO

      if target.nil?
        out << Violation.new(path, "unresolved", soname)
        next
      end

      next if allowed_ref?(target, allowed)
      out << Violation.new(path, "resolved outside", "#{soname} => #{target}")
    end

    return out
  end

  # Is this an ELF file? Read the magic rather than shelling out, so a
  # tree of thousands of files costs one open() each.
  def elf?(path)
    return false if !File.file?(path) || File.symlink?(path)
    File.open(path, "rb") { |f| f.read(4) == "\x7fELF".b }
  rescue SystemCallError
    false
  end

  # --- reading actual files ------------------------------------------

  # What one ELF file declares: its interpreter and its RPATH/RUNPATH.
  #
  # Parsed out of readelf rather than by decoding the ELF ourselves:
  # we build binutils, so readelf is a tool we own, and a hand-rolled
  # parser would be a second thing to get right.
  def read_refs(path, readelf:)

    interp = nil
    rpaths = []

    out = `#{readelf} -lWd #{path.to_s.shellescape} 2>/dev/null`

    if (m = out.match(/\[Requesting program interpreter: ([^\]]+)\]/))
      interp = m[1]
    end

    out.scan(/\((?:RPATH|RUNPATH)\).*?\[([^\]]*)\]/) do |(val)|
      rpaths.concat(val.split(":"))
    end

    return { interp: interp, rpaths: rpaths }
  end

  # Library directories a hostile environment would point at. Used to
  # make the resolution check independent of the environment: see
  # resolve_libs.
  SYSTEM_LIBDIRS = [
    "/usr/lib/x86_64-linux-gnu", "/lib/x86_64-linux-gnu",
    "/usr/lib64", "/lib64", "/usr/lib", "/lib",
  ].freeze

  # Where each shared library actually resolves, asked of the loader
  # itself rather than reimplemented. This is the ground truth: RPATH
  # and interpreter can both look right while a library still comes
  # from the system.
  #
  # Asked with LD_LIBRARY_PATH pointing at the system's library
  # directories, deliberately. Resolving correctly in a clean
  # environment proves very little: our loader has the sysroot compiled
  # in as its default search path, so a binary carrying no RPATH at all
  # resolves correctly when nothing competes and silently loads system
  # libraries when something does. Asking the hostile question instead
  # tests the property actually wanted — that the binary itself says
  # where its libraries live — and a binary that passes this passes the
  # clean case too, since DT_RPATH outranks LD_LIBRARY_PATH.
  #
  # Returns nil when the file cannot be inspected this way (a static
  # binary, or a loader that refuses it), which the caller treats as
  # "nothing to check" rather than as a pass.
  def resolve_libs(path, loader:, hostile: true)

    env = hostile ? "LD_LIBRARY_PATH=#{SYSTEM_LIBDIRS.join(":")} " : ""
    out = `#{env}#{loader.to_s.shellescape} --list #{path.to_s.shellescape} 2>/dev/null`
    return nil if out.strip.empty?

    resolved = {}

    for line in out.lines
      if (m = line.match(/^\s*(\S+)\s+=>\s+(\S+)/))
        resolved[m[1]] = (m[2] == "not" ? nil : m[2])
      elsif (m = line.match(/^\s*(\S+)\s+\(0x[0-9a-f]+\)/))
        name = m[1]
        next if name == VDSO

        # A bare entry with a load address and no "=>" is already an
        # absolute path — the loader listing itself. It resolved to
        # where it is; only a "=> not found" is unresolved.
        resolved[name] = name if name.start_with?("/")
      end
    end

    return resolved
  end

  # Audit every ELF file under `root`. Returns a list of Violations,
  # empty when the tree is clean.
  def audit(root, allowed:, readelf: "readelf", loader: nil,
            hostile: true)

    out = []

    Dir.glob("**/*", base: root.to_s).each do |rel|
      path = File.join(root.to_s, rel)
      next if !elf?(path)

      refs = read_refs(path, readelf: readelf)
      resolved = loader ?
        resolve_libs(path, loader: loader, hostile: hostile) : nil

      out.concat(
        check_refs(path,
                   interp: refs[:interp],
                   rpaths: refs[:rpaths],
                   resolved: resolved || {},
                   allowed: allowed)
      )
    end

    return out
  end
end
