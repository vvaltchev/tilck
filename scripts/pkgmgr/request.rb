# SPDX-License-Identifier: BSD-2-Clause
#
# WHAT WAS ASKED, AS A VALUE.
#
# A command line, once parsed: the mode, what it names, and the
# modifiers. main.rb builds one from its options (Main.request_of)
# and hands it to the planner (Planner.step), which turns it into
# plans; the model (tests/model/model.rb) parses the same grammar on
# its own and answers the same type, so that the two can be asked the
# same question and compared on values.
#
#   mode     :install | :uninstall | :configure | :mark_manual |
#            :mark_auto | :upgrade | :rebuild | :autoremove | :clean |
#            :default (no mode: the Tilck stack and the upgrades) |
#            :list | :check_updates | :installable | :layout |
#            :context | :other (a mode the planner has no say in:
#            --deps, -L, the help)
#   targets  [[name, ver], ...] as typed: a name may be a short one
#            and a version a series; :all for ALL on either side; a
#            version nil when none was given
#   force    -f      dry  -d
#   arch     Architecture | :all | nil (-a)
#   board    String | :all | nil (-b): one arch's board, so a name
#            with -a ALL is refused (Planner.board_refusal)
#   cc       Version | :all | nil (-c)
#   stack    Version | nil (-H)
#   contrib  --contrib: the extras appended to the default set
#

require_relative 'version'

Request = Data.define(:mode, :targets, :force, :dry, :arch, :board, :cc,
                      :stack, :contrib) do
  def self.make(mode, targets: [], force: false, dry: false, arch: nil,
                board: nil, cc: nil, stack: nil, contrib: false)
    new(mode: mode, targets: targets, force: force, dry: dry, arch: arch,
        board: board, cc: cc, stack: stack, contrib: contrib)
  end

  # "name[:ver]" as typed, to a target: ALL on either side is :all,
  # and an empty version -- `-S arch` spells its compiler "gcc-arch-
  # musl:" -- is none.
  def self.target(word)
    n, v = word.split(":", 2)
    [n == "ALL" ? :all : n,
     v.blank? ? nil : (v == "ALL" ? :all : Ver(v))]
  end

  # -a ALL: every arch, as a scope or as a filter.
  def every_arch? = arch == :all

  # -b ALL: every board of the arch(es), as a scope or as a filter.
  def every_board? = board == :all


  def to_s
    words = targets.map { |n, v|
      "#{n == :all ? 'ALL' : n}#{v.nil? ? '' : ":#{v == :all ? 'ALL' : v}"}"
    }
    "#{mode} #{words.join(' ')}".strip
  end
end
