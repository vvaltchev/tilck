# SPDX-License-Identifier: BSD-2-Clause

#
# The composed hermetic sysroot: one directory tree that every hermetic
# package's headers and libraries appear in, built as a symlink farm
# over the packages' own install directories.
#
# Packages keep installing into <pkg>/<ver>/install/ the way every
# other package in the tree does, so versions stay independently
# installable and removable. The sysroot is a VIEW over the versions a
# resolution selected — the version solver decides which, this renders
# the decision.
#
# A merged prefix rather than a per-package -I/-L list is not a
# convenience: a 95-library GTK stack contains packages that assume a
# single prefix, and a per-package -L list produces an RPATH nothing
# can use. It is also what lets a package bake an absolute --prefix
# naming the sysroot and have that path be true.
#
# Composition is a full rebuild rather than an update. Stale links from
# a version that is no longer selected are the failure mode that would
# be hardest to notice, and rebuilding a few thousand symlinks costs
# milliseconds.
#
# Pure filesystem work: no Package objects, no global state.
#

require 'fileutils'
require 'set'

module Sysroot

  # Two packages installing the same file. Left as an error rather than
  # a last-one-wins merge: which of the two won would depend on install
  # order, and the result would differ between machines.
  class ConflictError < StandardError; end

  module_function

  # Build `target` as a symlink farm over `fragments`.
  #
  # A fragment is either a directory that is already sysroot-shaped
  # (usr/include/..., usr/lib/...), or a [dir, subpath] pair grafting
  # dir at <target>/<subpath>. The pair form exists for packages whose
  # own layout is not sysroot-shaped but part of which belongs in the
  # sysroot anyway: GCC keeps libstdc++ and libgcc_s in lib64/, and
  # they are built against our glibc even though GCC itself is not a
  # hermetic package.
  #
  # Directories are recreated as real directories so two fragments can
  # contribute to the same one; files become symlinks to the fragment
  # that owns them.
  #
  # Returns the number of links created.
  def compose(target, fragments)

    target = Pathname.new(target.to_s)
    FileUtils.rm_rf(target.to_s)
    FileUtils.mkdir_p(target.to_s)

    owner = {}      # relative path => fragment that provided it
    links = 0

    for frag in fragments
      dir, sub = frag.is_a?(Array) ? frag : [frag, nil]
      dir = Pathname.new(dir.to_s)
      next if !dir.directory?

      dest = sub ? target / sub : target
      FileUtils.mkdir_p(dest.to_s)

      links += link_tree(dir, dest, owner, sub)
    end

    return links
  end

  # Walk one fragment, mirroring its directories and linking its files.
  def link_tree(frag, target, owner, prefix = nil)

    links = 0

    Dir.glob("**/*", File::FNM_DOTMATCH, base: frag.to_s).each do |rel|
      next if rel == "." || rel.end_with?("/.", "/..")

      src = frag / rel
      dst = target / rel

      # A directory in two fragments is normal: usr/lib holds files
      # from every library. Mirror it rather than linking it, or the
      # second fragment would have nowhere to put its files.
      if src.directory? && !src.symlink?
        FileUtils.mkdir_p(dst.to_s)
        next
      end

      key = prefix ? File.join(prefix.to_s, rel) : rel

      if owner.key?(key)
        raise ConflictError,
              "#{key} is provided by both #{owner[key]} and #{frag}"
      end

      owner[key] = frag
      FileUtils.mkdir_p(dst.dirname.to_s)
      FileUtils.ln_s(src.to_s, dst.to_s)
      links += 1
    end

    return links
  end
end
