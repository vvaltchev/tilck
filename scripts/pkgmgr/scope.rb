# SPDX-License-Identifier: BSD-2-Clause
#
# WHAT AN INVOCATION RESOLVES TO: the arch and board it builds for,
# the host stack it builds into, and the environment it started from.
#
# Every logic bug this package manager has had was a question about
# one installation answered from ambient state -- the global ARCH,
# the global BOARD, the stack that happened to be current. This is
# that state as a VALUE: built once at the CLI boundary, passed to
# whoever asks a scoped question, never read from a global by the
# code that decides. A package answers scoped questions only once it
# is bound to one of these (Package#at), so the scope an answer was
# computed under is visible at the call site that asked.
#
# board_of(a) is the one rule about boards: the scoped board for the
# scoped arch, the shell's BOARD for the shell's ARCH, an arch's own
# default otherwise. It is the model's rule too (tests/model/model.rb
# reuses this class), and it corrects a quirk the ivar-based scoping
# had: a board opened for one arch does not leak onto another arch
# opened inside it.
#

require_relative 'early_logic'
require_relative 'arch'

Scope = Data.define(:arch, :board, :stack, :env_arch, :env_board,
                    :host_os, :host_arch) do

  # The scope the environment describes: ARCH and BOARD as the shell
  # set them, the stack named or defaulted. The one place the
  # constants become a value.
  def self.env(stack:)
    board = BOARD.blank? ? nil : BOARD
    return new(arch: ARCH, board: board || ARCH.default_board, stack: stack,
               env_arch: ARCH, env_board: board,
               host_os: HOST_OS, host_arch: HOST_ARCH.name)
  end

  def board_of(a)
    return board     if a == arch
    return env_board if a == env_arch && env_board
    return a.default_board
  end

  # The same environment, at another arch (whose board follows the
  # rule above unless named), or in another stack.
  def with(arch: self.arch, board: nil, stack: self.stack)
    board ||= arch == self.arch ? self.board : board_of(arch)
    return Scope.new(arch: arch, board: board, stack: stack,
                     env_arch: env_arch, env_board: env_board,
                     host_os: host_os, host_arch: host_arch)
  end

  def to_s = "#{arch.name}/#{board} gcc-#{stack}"
end
