# SPDX-License-Identifier: BSD-2-Clause
#
# ONE PACKAGE MANAGER AT A TIME, PER THING.
#
# Two package managers on one tree -- two shells, a tree shared over
# TCROOT_PARENT, two CI jobs -- used to corrupt each other in every
# place they both wrote: the one temporary directory under the cache,
# a download's partial file, a package's staging directory, the
# cache's own record. Each of those is one thing, and one thing is
# held by one process at a time: an advisory lock on a small file
# named for it, taken for the block, released with the process
# whatever happens to it.
#
# flock(2), through File#flock: the same call on Linux, FreeBSD and
# macOS, and on NFS v4, where a tree is most likely to be shared. A
# lock is exclusive unless the holder only reads -- an extraction --
# and then it is shared, so that readers do not wait on each other
# and a writer waits on all of them. Nothing is ever deleted from the
# lock directory: a lock file is a name, not a resource, and removing
# one under a holder would let a second holder in through a new
# inode.
#
# The lock directory is under the cache, which --clean keeps: a
# process holding a build lock across a clean of the staging area
# still holds it.
#
require 'fileutils'

module Lock

  DIR = ".locks"

  module_function

  # The lock named `name` under `dir`, held for the block, which is
  # told whether it waited: what a holder finds after a wait was
  # another's doing, and what it finds without one was there before.
  # `what` names the thing for the message a waiting process prints,
  # since a wait that says nothing looks like a hang.
  def held(dir, name, shared: false, what: nil)
    FileUtils.mkdir_p(dir)
    File.open(File.join(dir, "#{name}.lock"), File::RDWR | File::CREAT,
              0644) do |f|
      mode = shared ? File::LOCK_SH : File::LOCK_EX
      waited = !f.flock(mode | File::LOCK_NB)
      if waited
        info "Waiting for another package manager: #{what || name}"
        f.flock(mode)
      end
      return yield(waited)
    end
  end
end
