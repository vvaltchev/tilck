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

  # Build `target` as a symlink farm over `fragments`, each of which is
  # a sysroot-shaped directory (usr/include/..., usr/lib/...).
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
      frag = Pathname.new(frag.to_s)
      next if !frag.directory?

      links += link_tree(frag, target, owner)
    end

    return links
  end

  # Walk one fragment, mirroring its directories and linking its files.
  def link_tree(frag, target, owner)

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

      if owner.key?(rel)
        raise ConflictError,
              "#{rel} is provided by both #{owner[rel]} and #{frag}"
      end

      owner[rel] = frag
      FileUtils.mkdir_p(dst.dirname.to_s)
      FileUtils.ln_s(src.to_s, dst.to_s)
      links += 1
    end

    return links
  end
end
