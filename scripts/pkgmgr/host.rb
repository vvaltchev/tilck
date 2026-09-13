# SPDX-License-Identifier: BSD-2-Clause
#
# THE BUILD HOST, AS A VALUE.
#
# Where this invocation runs: the OS and arch (together the <machine>
# coordinate of everything built to run here), the distro (the <env>
# of a package that links the distro's libraries) and the system
# compiler (the <stack> of one that depends on its C++ ABI). What the
# arch and the board became in Scope, the host becomes here: built
# once from the constants at the CLI boundary (Scope.env, through
# Host.env, the one place they are read) and carried by the scope,
# so that a placement question is answered from the host the scope
# names, and the model and the exhaustive lane can name another one.
#
# The distro's spelling (ID-VERSION_ID today) is a separate question,
# and this value is where its answer will change.
#
require_relative 'early_logic'
require_relative 'arch'

Host = Data.define(:os, :arch, :distro, :cc) do

  # The host this process runs on, from the constants.
  def self.env = new(os: HOST_OS, arch: HOST_ARCH, distro: HOST_DISTRO,
                     cc: HOST_CC)

  # The <machine> coordinate of what runs here.
  def machine = "#{os}-#{arch.name}"

  def to_s = "#{machine} #{distro} #{cc}"
end
