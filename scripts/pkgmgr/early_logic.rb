# SPDX-License-Identifier: BSD-2-Clause

MIN_RUBY_VERSION = "3.2.0"
DEFAULT_TC_NAME = "toolchain3"

def check_version
  # Convert a version to string to an array of integers.
  v2a = ->(s) { s.split(".").map(&:to_i) }

  ver = v2a.(RUBY_VERSION)
  min_ver = v2a.(MIN_RUBY_VERSION)

  if (ver <=> min_ver) < 0
    puts "ERROR: Ruby #{RUBY_VERSION} < #{MIN_RUBY_VERSION} (required)"
    exit 1
  end
end

check_version
require "power_assert"

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
      "amd64" => "x86_64"
    }

    tilck_name = table[arch]
    obj = ALL_HOST_ARCHS[arch] || ALL_HOST_ARCHS[tilck_name]

    if !obj
      puts "ERROR: host architecture #{arch} not supported"
      exit 1
    end

    return obj
  end

  def get_arch(arch)
    obj = ALL_ARCHS[arch]
    if !obj
      puts "ERROR: architecture #{arch} not supported"
      exit 1
    end
    return obj
  end

end
