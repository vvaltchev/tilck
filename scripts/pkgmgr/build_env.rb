# SPDX-License-Identifier: BSD-2-Clause

#
# What a package publishes to its dependents when it is used as a build
# dependency.
#
# Providers describe *what* they offer (include dirs, lib dirs, pkg-config
# dirs, extra environment variables); consumers decide *how* to spell it:
# kconfig wants HOSTCFLAGS/HOSTLDFLAGS as make variables, autotools wants
# CPPFLAGS in the environment, meson wants something else again.
#
# Keeping the data neutral is what makes merging possible at all. A
# rendered string can only be replaced: two providers each handing over
# "HOSTCFLAGS=-I..." end up on the same `make` command line, where GNU
# make keeps the last assignment and silently drops the first provider's
# include paths. A list can be combined, and the rendering happens once,
# at the end, with every path present.
#
# The merge is order-preserving and de-duplicating: a dependency reached
# through two different paths contributes its paths once, in the position
# where it was first seen.
#
# No Package objects, no filesystem, no global state.
#

class BuildEnv

  # Two providers disagreeing on the value of the same environment
  # variable. Silently keeping one of them is how a package ends up
  # built against a dependency nobody selected.
  class ConflictError < StandardError; end

  attr_reader :include_dirs, :lib_dirs, :pkg_config_dirs, :extra_env

  def initialize(include_dirs: [], lib_dirs: [],
                 pkg_config_dirs: [], extra_env: {})

    @include_dirs    = BuildEnv.normalize(include_dirs)
    @lib_dirs        = BuildEnv.normalize(lib_dirs)
    @pkg_config_dirs = BuildEnv.normalize(pkg_config_dirs)
    @extra_env       = extra_env.map { |k, v| [k.to_s, v.to_s] }.to_h.freeze

    freeze
  end

  # Accept a single Pathname/String or a list of them. Values are
  # stringified (Pathname doesn't survive a `system()` env hash) and
  # de-duplicated, keeping the first occurrence.
  def self.normalize(v)
    return [].freeze if v.nil?
    list = v.is_a?(Array) ? v : [v]
    return list.map { |x| x.to_s }.uniq.freeze
  end

  def self.empty = new

  def empty?
    return @include_dirs.empty? && @lib_dirs.empty? &&
           @pkg_config_dirs.empty? && @extra_env.empty?
  end

  # Combine two build environments, `self`'s entries first. Consumers
  # merge their nearest dependencies first, so a direct dependency's
  # include paths come ahead of the transitive ones.
  def merge(other)

    return self if other.nil? || other.empty?
    return other if empty?

    env = @extra_env.dup

    for k, v in other.extra_env
      if env.key?(k) && env[k] != v
        raise ConflictError,
              "Conflicting value for #{k}: '#{env[k]}' vs '#{v}'"
      end
      env[k] = v
    end

    return BuildEnv.new(
      include_dirs:    @include_dirs    + other.include_dirs,
      lib_dirs:        @lib_dirs        + other.lib_dirs,
      pkg_config_dirs: @pkg_config_dirs + other.pkg_config_dirs,
      extra_env:       env,
    )
  end

  # --- Rendered forms: pick the one the consumer's build system wants ---

  def cflags = @include_dirs.map { |d| "-I#{d}" }.join(" ")
  def ldflags = @lib_dirs.map { |d| "-L#{d}" }.join(" ")

  # Environment for a child process. PKG_CONFIG_PATH is prepended to the
  # inherited value so our .pc files win over the system ones without
  # hiding them entirely.
  def env(base = ENV)

    e = @extra_env.dup
    return e if @pkg_config_dirs.empty?

    parts = @pkg_config_dirs.dup
    inherited = base["PKG_CONFIG_PATH"]
    parts << inherited if inherited && !inherited.empty?

    e["PKG_CONFIG_PATH"] = parts.join(":")
    return e
  end

  # Make variables for kconfig's host tools (mconf, conf, lxdialog), as
  # used by `make menuconfig` in busybox and u-boot. A variable is only
  # emitted when we have something to say: passing an empty
  # HOSTCFLAGS= on the command line would override the Makefile's own
  # value with nothing.
  def kconfig_make_vars

    vars = []
    vars << "HOSTCFLAGS=#{cflags}" if !@include_dirs.empty?
    vars << "HOSTLDFLAGS=#{ldflags}" if !@lib_dirs.empty?

    return vars
  end
end
