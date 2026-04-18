# SPDX-License-Identifier: BSD-2-Clause

MIN_RUBY_VERSION = "3.2.0"

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

