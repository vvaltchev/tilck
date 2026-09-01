# SPDX-License-Identifier: BSD-2-Clause

require_relative 'early_logic'
require_relative 'system_deps'

#
# Driving cargo with our own toolchain.
#
# Mixed into the packages whose build system calls cargo. It names
# Rust and nothing else: no package appears here, and no Rust
# knowledge goes into Package, which must not learn about one
# language on behalf of two of its subclasses.
#
# What every such package needs is the same, and it is not obvious:
#
#   * rustc and cargo come from the HOST, via rustup. We do not build
#     a Rust toolchain -- rustup's is better than ours would be, and a
#     compiler is a large thing to build for an image loader. They are
#     declared as system dependencies instead, so a machine without
#     them is told before the build starts.
#
#   * the artifacts we ship must link against OUR glibc, or they are
#     not portable and cannot sit in portable/ -- which would drag
#     everything downstream of them into the distro tree.
#
# The second is where the subtlety lives. See with_cargo_env.
#
module CargoBuild

  # The triple cargo builds for. A Rust target names the machine the
  # artifact runs on, which is this one: we are not cross-compiling,
  # only redirecting the linker.
  def cargo_triple
    case HOST_ARCH.name
      when "x86_64"  then "x86_64-unknown-linux-gnu"
      when "aarch64" then "aarch64-unknown-linux-gnu"
    else
      raise "#{name}: no Rust target triple known for #{HOST_ARCH.name}"
    end
  end

  def rust_system_deps = [SystemDeps::RUSTC, SystemDeps::CARGO]

  # Where the host's cargo and rustc are, or nil if either is absent.
  def rust_tools
    env = SystemDeps::Env.new
    cargo = env.which("cargo")
    rustc = env.which("rustc")
    return nil if cargo.nil? || rustc.nil?
    return [cargo, rustc]
  end

  #
  # Run a cargo-driven build with our compiler linking the artifacts.
  #
  # The linker is set through RUSTFLAGS and NOT through
  # CARGO_TARGET_<triple>_LINKER, which is the obvious way and is
  # wrong. Measured: that variable applies to HOST units as well as
  # target ones, with --target given or not. Host units are the
  # proc-macros, which are compiled for the host and then dlopen'd by
  # rustc itself -- so linking them against our glibc loads a second
  # libc into a system rustc running under the system one, and the
  # build dies with
  #
  #   error: cannot determine resolution for the import   (E0463)
  #
  # out of whichever crate happens to use a proc-macro first, saying
  # nothing at all about the real cause.
  #
  # RUSTFLAGS is scoped: with --target passed, cargo gives it only to
  # the target units. Proc-macros keep the system cc, the library we
  # ship gets our gcc, our glibc, our RPATH and our loader.
  #
  # Passing --target is therefore not optional, and each build system
  # has its own way of being told to (a meson cross file, a triplet
  # option); that part belongs to the package.
  #
  def with_cargo_env(&block)

    tools = rust_tools

    if tools.nil?
      error "#{name}: cargo and rustc must both be on the host"
      return false
    end

    cargo, _ = tools
    gcc_bin, _ = stack_toolchain_bins

    # Set outside with_stack_toolchain, which the build helpers enter:
    # that rebuilds PATH from ENV["PATH"], so what is prepended here
    # survives, behind our own compiler rather than ahead of it.
    return with_saved_env(["PATH", "RUSTFLAGS"]) do
      ENV["PATH"] = "#{File.dirname(cargo)}:#{ENV["PATH"]}"
      ENV["RUSTFLAGS"] = "-C linker=#{gcc_bin}/gcc"
      block.call
    end
  end

  #
  # A meson cross file for a build that is not cross-compiling.
  #
  # For the packages whose meson.build passes --target to cargo only
  # under meson.is_cross_build(). needs_exe_wrapper=false because the
  # "cross" target is this very machine and meson may run what it
  # builds.
  #
  # Where the cross file goes. Separate from writing it so that
  # build_flags can name it without side effects: the flags are asked
  # for during staleness checks too, when no build is running.
  def cargo_cross_file_path = File.expand_path("tilck-cross.ini")

  def write_cargo_cross_file

    _, rustc = rust_tools
    gcc_bin, bu_bin = stack_toolchain_bins
    full = cargo_cross_file_path

    File.write(full, <<~CROSS)
      [binaries]
      c = '#{gcc_bin}/gcc'
      cpp = '#{gcc_bin}/g++'
      ar = '#{bu_bin}/ar'
      strip = '#{bu_bin}/strip'
      pkg-config = 'pkg-config'
      rust = '#{rustc}'

      [properties]
      needs_exe_wrapper = false
      rust_target = '#{cargo_triple}'

      [host_machine]
      system = 'linux'
      cpu_family = '#{HOST_ARCH.name}'
      cpu = '#{HOST_ARCH.name}'
      endian = 'little'
    CROSS

    return full
  end
end
