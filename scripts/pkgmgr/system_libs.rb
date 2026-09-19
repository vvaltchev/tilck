# SPDX-License-Identifier: BSD-2-Clause
#
# WHICH OF THE HOST'S SHARED LIBRARIES AN INSTALL'S BINARIES RESOLVE TO.
#
# A host package of the :distro or :compiler tier links the distro's
# libraries by design (Package#host_tier), and what it was built
# against is those files, as they were. On a fixed-release distro the
# release names that set; on a rolling one nothing does, and the files
# move on the distro's schedule while the binaries stay. So the files
# are recorded, each with its digest, beside the install in
# .build_inputs (BuildInputs, syslib lines), and an install whose
# recorded libraries have changed under it reads as stale, the way
# one whose patches changed does: --check-for-updates lists it under
# NEEDS_REBUILD and --rebuild builds it again against what is there
# now.
#
# Asked of the loader, not reimplemented: `ldd` lists what each
# soname resolves to on THIS machine, following RPATH and the loader's
# own search path. Only paths outside the toolchain are kept, since
# what is inside it is recorded by the tree itself; the loader's
# entry (a bare absolute path, no "=>") is kept too, since the
# interpreter is the one library every dynamic binary needs.
#
require 'digest'
require 'shellwords'
require 'set'

module SystemLibs

  module_function

  # {absolute path => sha256 digest} of every host library the ELF
  # files under `dir` resolve to, `tc` excluded. Symlinks are resolved
  # first, so that a library replaced behind its soname link is seen
  # as the new file it is.
  def of_install(dir, tc: TC)
    paths = Set.new
    for f in Dir.glob("#{dir}/**/*") do
      # A directory, a symlink (the same file, already seen through
      # its own name) and a non-ELF file each contribute nothing
      # either way: elf? reads four bytes and ldd refuses what is not
      # an object, so the two guards here are for the cost, not the
      # answer.
      # mutation: equivalent -- elf? and ldd reject what this skips
      next if !File.file?(f) || File.symlink?(f)
      # mutation: equivalent -- ldd of a non-ELF yields nothing
      next if !elf?(f)
      for lib in resolved_by(f) do
        real = File.realpath(lib) rescue next
        next if real.start_with?(tc.to_s + "/")
        paths << real
      end
    end
    return paths.sort.to_h { |p| [p, digest(p)] }
  end

  def digest(path) = "sha256:" + Digest::SHA256.file(path).hexdigest[0, 32]

  # The four magic bytes: cheaper than shelling out on every file.
  def elf?(path)
    File.open(path, "rb") { |io| io.read(4) } == "\x7fELF".b
  rescue SystemCallError
    false
  end

  # What the loader resolves for one binary: the "=> /path" entries
  # and the loader's own line. An object the loader refuses (a
  # foreign ELF, a static binary) contributes nothing.
  def resolved_by(path)
    out = `ldd #{path.to_s.shellescape} 2>/dev/null`
    libs = out.scan(/=> (\/\S+)/).flatten
    libs += out.scan(/^\s*(\/\S+) \(0x/).flatten
    return libs
  end
end
