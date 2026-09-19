# SPDX-License-Identifier: BSD-2-Clause
#
# THE ONE SHAPE OF EVERY RECORD THE PACKAGE MANAGER WRITES.
#
#   key: value
#
# One fact per line, the key an identifier, the value everything
# after the first ": " with its ends trimmed. A key may repeat, and
# then its values accumulate in order (`against:` names one
# dependency per line). A key a reader does not ask for is ignored,
# which is how a later format adds a field without breaking a reader
# of this one; a line with no separator is a broken file and the
# whole record is refused, since a missing separator is not an
# unknown key. Every record starts with `format: N`.
#
# Three files use it: .install beside every installation, stack.conf
# at every stack's root, .build_inputs beside every installation. A
# value may contain spaces and colons -- a host is three words, a
# digest is sha256:... -- because only the first ": " separates.
#
module Record

  SEP = ": "

  module_function

  # The file as {key => [values]}, or nil where there is no file or
  # a line has no separator.
  def read(path)
    return nil if !File.file?(path)
    return parse(File.read(path))
  end

  def parse(text)
    kv = Hash.new { |h, k| h[k] = [] }
    for line in text.lines do
      line = line.chomp
      next if line.strip.empty?
      k, v = line.split(SEP, 2)
      return nil if v.nil? || k !~ /\A[a-z_]+\z/
      kv[k] << v.strip
    end
    return kv
  end

  # `pairs` as the text of a record: [[key, value], ...], in the
  # order given, a list-valued key spelled once per value.
  def render(pairs)
    return pairs.flat_map { |k, v|
      Array(v).map { |x| "#{k}#{SEP}#{x}" }
    }.join("\n") + "\n"
  end

  def write(path, pairs)
    FileUtils.mkdir_p(File.dirname(path))
    File.write(path, render(pairs))
  end

  # The one value of `key`, or nil.
  def one(kv, key) = kv && kv[key]&.first

  # The format a record says it has: nil where there is no record,
  # 0 where it says none.
  def format_of(kv) = kv.nil? ? nil : one(kv, "format").to_i
end
