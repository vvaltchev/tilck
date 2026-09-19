# SPDX-License-Identifier: BSD-2-Clause

require 'open3'

#
# WHAT MUST BE TRUE OF AN INSTALL, once it is in place.
#
# A package declares two halves of that. expected_files is the
# structural half -- these paths exist -- cheap enough to ask of every
# install on every scan, which is what "broken" means in a listing.
# This is the behavioural half: the installed meson starts; the gcc
# that was just built produces portable binaries.
#
# A postcondition is checked ONCE, after the atomic move, against the
# install where it lives -- meson's launcher names its final path, and
# would fail from staging for a reason that is not a fault. It never
# runs during a scan: compiling a test program is not a thing -l does.
# And it is NEVER part of the recipe digest. A check installs nothing,
# so tightening one must not invalidate an artifact -- the next install
# is checked harder, which is the point of tightening it.
#
# That last property is why a postcondition may be any object at all:
# a kind here for the common case, or a class beside the package that
# owns the knowledge (host_gcc's portability audit is host_gcc's).
#
module Postcondition

  class Base
    # True when the install is what it should be. Reports what is
    # wrong through error, so that the message names the package.
    def check(pkg, dir) = raise NotImplementedError
    def describe = self.class.name
  end

  #
  # The installed program starts. argv is relative to the install's
  # version directory, the way expected_files entries are, and is
  # expanded, so it may name $PYTHON.
  #
  class Runs < Base

    attr_reader :argv

    def initialize(argv:)
      raise ArgumentError, "Runs: argv must be a non-empty array" if
        !argv.is_a?(Array) || argv.empty?
      @argv = argv.map(&:to_s).freeze
      freeze
    end

    def check(pkg, dir)

      ctx = Package::BuildCtx.new(pkg, dir)
      cmd = ctx.expand_all(@argv)

      begin
        st, out = ctx.run_argv(cmd, capture: :both)
      rescue SystemCallError => e
        error "#{pkg.name}: #{cmd.first} cannot be run: #{e.message}"
        return false
      end

      if st != 0
        error "#{pkg.name}: #{cmd.first} does not run (exit #{st}):\n" +
              out.strip
        return false
      end

      first = out.strip.lines.first.to_s.strip
      info "#{File.basename(cmd.first)} runs" +
           (first.empty? ? "" : ": #{first}")
      return true
    end

    def describe = "runs #{@argv.join(" ")}"
  end

  # Runs(argv: [...]) inside a package, like the recipe's constructors.
  module DSL
    def Runs(**kw) = Postcondition::Runs.new(**kw)
  end
end
