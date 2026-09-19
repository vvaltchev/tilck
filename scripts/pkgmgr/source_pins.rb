# SPDX-License-Identifier: BSD-2-Clause
#
# WHAT EVERY SOURCE MUST BE: other/pkg_hashes.
#
# A source is declared as a URL and a name (SourceRef), and until
# this file that was the whole of it: whatever answered at the URL
# was the source, and whatever was in the cache under the name was
# trusted. This file pins each cache file to one thing, by the name
# the cache knows it under -- the one key every package, host and
# fetch mode already agrees on -- and the package manager refuses a
# file that is anything else.
#
# Two kinds of pin, told apart by their prefix, because two kinds of
# file reach the cache:
#
#   sha256:<64 hex>   a file downloaded as it is: the digest of the
#                     bytes, comparable by eye with an upstream's
#                     published SHA256SUMS
#   git:<40 hex>      a source we clone and pack ourselves: the
#                     commit the clone must resolve to. Not the
#                     archive's digest, since the archive is ours and
#                     its bytes depend on how we pack it; the commit
#                     is what upstream published, and it survives a
#                     change of compression
#
# A name is a cache filename, looked up exactly as written and never
# parsed; nothing here knows what a name means, the registry does.
# A line is the whole of what the tool asks a person to add when a
# source is new, and it prints the line ready to paste.
#
require 'digest'
require_relative 'early_logic'
require_relative 'table'

module SourcePins

  FILE = MAIN_DIR / "other" / "pkg_hashes"

  # The one shape of each kind's value.
  KINDS = {
    sha256: /\A[0-9a-f]{64}\z/,
    git:    /\A[0-9a-f]{40}\z/,
  }.freeze

  Pin = Data.define(:kind, :value) do
    def to_s = "#{kind}:#{value}"
  end

  class Malformed < StandardError; end

  module_function

  # A pin from its spelling, or nil where the spelling is not one.
  def parse_pin(str)
    kind, value = str.to_s.split(":", 2)
    # mutation: equivalent -- a nil value matches no KINDS pattern below
    return nil if kind.nil? || value.nil?
    kind = kind.to_sym
    return nil if !KINDS.key?(kind) || value !~ KINDS[kind]
    return Pin.new(kind: kind, value: value)
  end

  # The file's text as {name => Pin}. A line whose value is not a
  # pin is refused by line number, like a line that is not a line.
  def parse(text)
    table = begin
      Table.parse(text)
    rescue Table::Malformed => e
      raise Malformed, e.message
    end
    return table.to_h { |name, v|
      pin = parse_pin(v)
      raise Malformed, "#{name}: not a pin: #{v.inspect}" if pin.nil?
      [name, pin]
    }
  end

  # The pins at `path`, or none where there is no file yet.
  def load(path = FILE)
    return {} if !File.file?(path)
    return parse(File.read(path))
  rescue Malformed => e
    raise Malformed, "#{path}: #{e.message}"
  end

  # The line that pins `name` to `pin`, as the file spells it.
  def line(name, pin) = "#{name}#{Table::SEP}#{pin}"

  # The digest of the bytes at `path`, as a pin.
  def digest_of(path)
    Pin.new(kind: :sha256, value: Digest::SHA256.file(path.to_s).hexdigest)
  end

  # A commit as a pin, or nil where the string is not a full one.
  def commit(sha) = parse_pin("git:#{sha.to_s.strip}")
end
