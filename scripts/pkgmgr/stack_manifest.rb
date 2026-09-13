# SPDX-License-Identifier: BSD-2-Clause
#
# THE STACK MANIFEST: what a stack is made of, written where it lives.
#
#   <machine>/<env>/<stack>/stack.conf
#
#   format      1
#   kind        host | target
#   compiler    <package> <version>
#   compiler_at <machine>/<env>/<stack>     host stacks: where THIS
#                                           host's install of it is
#   libc        <package> <version>
#   host        <machine> <distro> <cc>     host stacks: who built it
#
# A stack's compiler does not live in the stack it defines: host_gcc
# is a :distro package under the distro's env, a cross compiler is a
# :portable one under the host's. The association was a naming
# coincidence -- the stack gcc-14.4.0 is host_gcc 14.4.0's because
# both spell 14.4.0 -- and it broke the moment the distro env moved:
# six stacks read as not built while their sysroots still held the
# grafted compiler. The manifest records the fact instead, in one
# format for both kinds of stack.
#
# The two kinds differ in one field. A host stack is one machine's,
# so its compiler has coordinates on this host and they are recorded
# as written. A target stack is host-independent by design -- a Linux
# and a Darwin host both build into tilck-i386/pc/gcc-13.3.0/, each
# with its own prebuilt gcc-i386-musl 13.3.0 -- so its compiler is
# named by identity and located through the registry on whichever
# host is asking; compiler_at stays empty.
#
# Written by the executor when a compiler is installed
# (Package#stacks_defined) and for stacks from before the record
# (Executor.write_missing_manifests); read by the listing and the
# sysroot composition. Unknown keys are ignored, so a later format
# may add fields without breaking a reader of this one.
#
require_relative 'coords'
require_relative 'version'

StackManifest = Data.define(:kind, :compiler_name, :compiler_ver,
                            :compiler_at, :libc, :libc_ver, :host)

class StackManifest
  FILE = "stack.conf"
  FORMAT = 1

  # The manifest at `coords`, or nil where there is none, or one of a
  # format this reader does not know.
  def self.read(coords)
    path = coords.root / FILE
    return nil if !path.file?

    # Every line is a key and a value; a blank line or a comment is a
    # key nothing asks for, which is all the skipping it needs.
    kv = {}
    for line in path.read.lines do
      k, v = line.strip.split(" ", 2)
      kv[k] = v.to_s.strip
    end
    return nil if kv["format"].to_i != FORMAT

    comp = kv["compiler"].to_s.split
    libc = kv["libc"].to_s.split
    return new(kind: kv["kind"].to_s.to_sym,
               compiler_name: comp[0],
               compiler_ver: SafeVer(comp[1].to_s),
               compiler_at: kv["compiler_at"],
               libc: libc[0], libc_ver: SafeVer(libc[1].to_s),
               host: kv["host"])
  end

  def self.write(coords, manifest)
    root = coords.root
    FileUtils.mkdir_p(root)
    File.write(root / FILE, manifest.render)
  end

  def render
    lines = ["format #{FORMAT}", "kind #{kind}",
             "compiler #{compiler_name} #{compiler_ver}"]
    lines << "compiler_at #{compiler_at}" if compiler_at
    lines << "libc #{libc} #{libc_ver}" if libc && libc_ver
    lines << "host #{host}" if host
    return lines.join("\n") + "\n"
  end

  # The coordinates of this host's install of the compiler, when the
  # manifest says where it is.
  def compiler_coords = compiler_at ? Coords.from_s(compiler_at) : nil

  def to_s = "#{compiler_name} #{compiler_ver}"
end
