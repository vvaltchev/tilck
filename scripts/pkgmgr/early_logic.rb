# SPDX-License-Identifier: BSD-2-Clause

require_relative 'arch'
require_relative 'term'

require 'power_assert'
require 'pathname'
require 'etc'
require 'shellwords'

# Global, generic constants.
KB = 1024
MB = 1024 * KB

# Basic constants specific to this project.
DEFAULT_TC_NAME = "toolchain4"
OS = Etc.uname.fetch(:sysname)
RUBY_SOURCE_DIR = Pathname.new(File.realpath(__dir__))
MAIN_DIR = Pathname.new(RUBY_SOURCE_DIR.parent.parent)
GITHUB = "https://github.com"

# Generic utils
def getenv(name, default)
  val = ENV[name].to_s
  return !val.empty? ? val : default
end

def assert(&expr)
  PowerAssert.start(expr, assertion_method: __method__) do |ctx|
    ok = ctx.yield
    raise "Assertion failed:\n#{ctx.message}" unless ok
    true
  end
end

def info(msg)
  infoStr = STDOUT.tty?? Term.makeBlue("INFO") : "INFO"
  puts "#{infoStr}: #{msg}"
end

def warning(msg)
  warnStr = STDOUT.tty?? Term.makeYellow("WARNING") : "WARNING"
  puts "#{warnStr}: #{msg}"
end

def error(msg)
  errStr = STDOUT.tty?? Term.makeRed("ERROR") : "ERROR"
  puts "#{errStr}: #{msg}"
end

def mkpathname(path_str) = Pathname.new(path_str)

def make_gh_rel_download(user, proj, tag)
  return "#{GITHUB}/#{user}/#{proj}/releases/download/#{tag}"
end

class LocalError < StandardError
end

module FileShortcuts
  module_function
  def exist?(...)      = File.exist?(...)
  def file?(...)       = File.file?(...)
  def symlink?(...)    = File.symlink?(...)
  def directory?(...)  = File.directory?(...)
  def readable?(...)   = File.readable?(...)
  def writable?(...)   = File.writable?(...)
  def executable?(...) = File.executable?(...)
  def basename(...)    = File.basename(...)
  def dirname(...)     = File.dirname(...)
  def extname(...)     = File.extname(...)
  def stat(...)        = File.stat(...)
  def lstat(...)       = File.lstat(...)
  def readlink(...)    = File.readlink(...)
  def expand_path(...) = File.expand_path(...)
  def realpath(...)    = File.realpath(...)
end

module FileUtilsShortcuts
  module_function
  def chdir(...)       = FileUtils.chdir(...)
  def getwd()          = FileUtils.getwd()
  def mkdir(...)       = FileUtils.mkdir(...)
  def mkdir_p(...)     = FileUtils.mkdir_p(...)
  def rm(...)          = FileUtils.rm(...)
  def rm_f(...)        = FileUtils.rm_f(...)
  def rm_r(...)        = FileUtils.rm_r(...)
  def rm_rf(...)       = FileUtils.rm_rf(...)
  def mv(...)          = FileUtils.mv(...)
  def symlink(...)     = FileUtils.symlink(...)
  def ln_s(...)        = FileUtils.ln_s(...)
  def ln_sf(...)       = FileUtils.ln_sf(...)
  def cp(...)          = FileUtils.cp(...)
  def cp_r(...)        = FileUtils.cp_r(...)
  def rmdir(...)       = FileUtils.rmdir(...)
end

# Monkey-patch String and NilClass to support blank? like in Rails.
class String
  def blank? = strip.empty?
  def _ = gsub("-", "_")  # Custom to this package manager
end

class NilClass
  def blank? = true
end

# Do the same for other built-in types, for convenience.
class TrueClass
  def blank? = false
end

class FalseClass
  def blank? = true
end

class Array
  def blank? = empty?
end

class Hash
  def blank? = empty?
end

class Object
  def blank? = false
end

def chdir!(path, &block)
  FileUtils.mkdir_p(path)
  block ? FileUtils.chdir(path, &block) : FileUtils.chdir(path)
end

module InitOnly

  module_function

  def get_tc_root
    parent = getenv("TCROOT_PARENT", MAIN_DIR)
    tcroot = getenv(
      "TCROOT", File.join(parent, DEFAULT_TC_NAME)
    )
    return Pathname.new(tcroot)
  end

  def get_host_arch(arch)

    # Translation table, necessary to handle the case where uname -m
    # returned "amd64" instead of "x86_64".
    table = {
      "amd64" => "x86_64",
      "arm64" => "aarch64"
    }

    tilck_name = table[arch]
    obj = ALL_HOST_ARCHS[arch] || ALL_HOST_ARCHS[tilck_name]

    if !obj
      error "Host architecture not supported: #{arch}"
      exit 1
    end

    return obj
  end

  def get_arch(arch)
    obj = ALL_ARCHS[arch]
    if !obj
      error "Architecture not supported: #{arch}"
      exit 1
    end
    return obj
  end

  # Map uname's sysname to a short, lowercase OS token used in paths.
  def get_host_os
    case OS
      when "Linux"   then "linux"
      when "Darwin"  then "macos"
      when "FreeBSD" then "freebsd"
    else
      error "Unsupported host OS: #{OS}"
      exit 1
    end
  end

  # Parse /etc/os-release into a { KEY => value } hash, stripping quotes.
  def parse_os_release
    path = "/etc/os-release"
    if !File.file?(path)
      error "#{path} not found: cannot detect the Linux distro"
      exit 1
    end
    data = {}
    File.read(path).each_line do |line|
      line = line.strip
      next if line.empty? || line.start_with?("#")
      k, v = line.split("=", 2)
      next if k.nil? || v.nil?
      v = v.sub(/\A"(.*)"\z/, '\1')
      data[k] = v
    end
    return data
  end

  # Return a distro slug like "ubuntu-22.04", "macos-14.3", "freebsd-14.0".
  # Different OS releases are considered incompatible: we do NOT try to share
  # dynamically-linked host packages across them.
  def get_host_distro(host_os)
    case host_os
      when "linux"
        data = parse_os_release()
        id = data["ID"]
        ver = data["VERSION_ID"]
        if id.nil? || ver.nil?
          error "/etc/os-release is missing ID or VERSION_ID"
          exit 1
        end
        return "#{id}-#{ver}"
      when "macos"
        ver = `sw_vers -productVersion 2>/dev/null`.strip
        if ver.empty?
          error "Could not run `sw_vers -productVersion`"
          exit 1
        end
        return "macos-#{ver}"
      when "freebsd"
        ver = `uname -r 2>/dev/null`.strip.split("-").first
        if ver.nil? || ver.empty?
          error "Could not run `uname -r`"
          exit 1
        end
        return "freebsd-#{ver}"
    else
      error "Unsupported host OS: #{host_os}"
      exit 1
    end
  end

  # Run `cc -v` and extract [family, version] from the output.
  # Both GCC and Clang emit "<family> version X.Y.Z" on stderr.
  def detect_cc_info(cc)
    out = `#{Shellwords.escape(cc)} -v 2>&1`
    if out.empty?
      error "Could not run compiler: #{cc}"
      exit 1
    end
    if (m = out.match(/clang version\s+(\d+(?:\.\d+)+)/i))
      return ["clang", m[1]]
    end
    if (m = out.match(/gcc version\s+(\d+(?:\.\d+)+)/i))
      return ["gcc", m[1]]
    end
    error "Cannot identify compiler: #{cc}"
    puts out
    exit 1
  end

  # Determine the host compiler family+version from $CC (defaults to "gcc").
  # If $CXX is also set, require that it points to the same family+version.
  # If only $CXX is set, fail with a clear message.
  def get_host_cc
    cc  = ENV["CC"].to_s
    cxx = ENV["CXX"].to_s

    if cc.empty? && !cxx.empty?
      error "CXX is set but CC is not."
      error "Please set CC so the package manager knows which host compiler"
      error "to use (it must match CXX)."
      exit 1
    end

    cc = "gcc" if cc.empty?
    cc_family, cc_ver = detect_cc_info(cc)

    if !cxx.empty?
      cxx_family, cxx_ver = detect_cc_info(cxx)
      if cxx_family != cc_family || cxx_ver != cc_ver
        error "CC and CXX refer to different compilers:"
        error "  CC  = #{cc} -> #{cc_family} #{cc_ver}"
        error "  CXX = #{cxx} -> #{cxx_family} #{cxx_ver}"
        error "They must point to the same family and version."
        exit 1
      end
    end

    return "#{cc_family}-#{cc_ver}"
  end

end

TC = InitOnly.get_tc_root()
TC_CACHE = TC / "cache"
TC_NOARCH = TC / "noarch"
ARCH = InitOnly.get_arch(getenv("ARCH", DEFAULT_ARCH))
HOST_ARCH = InitOnly.get_host_arch(Etc.uname[:machine])

# Host environment detection.
#
# HOST_OS      = "linux" | "macos" | "freebsd"
# HOST_DISTRO  = "ubuntu-22.04" | "macos-14.3" | "freebsd-14.0" | ...
# HOST_CC      = "gcc-13.3.0" | "clang-14.0.0" | ...
#
# The host toolchain layout is split into two halves:
#
#   HOST_DIR_PORTABLE   For 100% statically-linked host tools (e.g. the
#                       cross-compilers) that do not depend on the host
#                       distro or system libraries. Shared across distros
#                       and host compilers.
#
#   HOST_DIR            For dynamically-linked host tools (e.g. host_mtools,
#                       host_gtest) whose binaries or libraries depend on
#                       glibc/libstdc++ from a specific distro and were
#                       built by a specific host compiler. Because C++ has
#                       no stable ABI, the host-compiler version matters
#                       even for C tools that may depend on C++ host libs.
HOST_OS      = InitOnly.get_host_os()
HOST_DISTRO  = InitOnly.get_host_distro(HOST_OS)
HOST_CC      = InitOnly.get_host_cc()
HOST_OS_ARCH = "#{HOST_OS}-#{HOST_ARCH.name}"

HOST_DIR_PORTABLE = TC / "host" / HOST_OS_ARCH / "portable"
HOST_DIR          = TC / "host" / HOST_OS_ARCH / HOST_DISTRO / HOST_CC

DEFAULT_BOARD = ARCH.default_board
BOARD = ENV["BOARD"] || DEFAULT_BOARD
BOARD_BSP = BOARD ? MAIN_DIR / "other" / "bsp" / ARCH.name / BOARD : nil
BUILD_PAR = ENV["BUILD_PAR"] or ""

def get_human_arch_name(arch)
  return "noarch" if arch.nil?
  return "host" if arch == HOST_ARCH
  return arch.name
end

def prepend_to_global_path(path)
  assert { path.directory? }
  path = path.to_s()
  parts = (ENV["PATH"] || "").split(File::PATH_SEPARATOR)
  parts.unshift(path) unless parts.include?(path)
  ENV["PATH"] = parts.join(File::PATH_SEPARATOR)
end

def with_saved_env(vars, &block)
  saved = vars.to_h { |v| [v, ENV[v]] }
  block.call()
  saved.each { |k,v| ENV[k] = v }
end

def run_command(out, argv)
  assert { argv.is_a? Array }
  assert { argv.length > 0 }

  cmd_str = argv.map { |a| Shellwords.escape(a.to_s) }.join(" ")
  info "Run: #{cmd_str}"

  if !out
    ok = system(*argv)
  else
    File.open(out, "wb") do |fh|
      ok = system(*argv, out: fh, err: fh)
    end
  end

  error "Command failed" if !ok
  return ok
end

def stable_sort(array, &cmp)

  cmp ||= ->(a, b) { a <=> b }

  array.each_with_index
       .sort do |(a, ia), (b, ib)|
         c = cmp.call(a, b)
         c = 0 if c.nil? # be tolerant; Ruby expects -1/0/1,
                         # but some comparators return nil
         c == 0 ? (ia <=> ib) : c
       end
       .map(&:first)
end

