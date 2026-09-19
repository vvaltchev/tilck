# SPDX-License-Identifier: BSD-2-Clause
#
# A TABLE OF NAMES, the second shape the package manager writes.
#
#   name: value
#
# Where a Record (record.rb) is one thing described by its fields,
# a table is many things named once each: a cache file and what it
# must be. The separator is the same, the key is not: a filename,
# with the characters a filename may have and no other, rather than
# an identifier. A name appears once, or the table is refused -- two
# lines for one file would be two answers. A `#` line is a comment
# and a blank line is nothing, so that a table kept in the tree can
# say what it is at its top.
#
# Two files use it: other/pkg_hashes, what every source must be, and
# cache/.hashes, what every cached file was when it was placed.
#
module Table

  class Malformed < StandardError; end

  SEP = ": "

  # The characters a name may have. Nothing else: a name is looked
  # up exactly as written, never parsed, and this is what keeps it
  # safe on one line beside its value on every host.
  NAME = /\A[A-Za-z0-9._+-]+\z/

  module_function

  # The text as {name => value}. A line without the separator, a
  # name outside NAME or a name given twice is a broken table, and
  # says which line.
  def parse(text)
    out = {}
    text.each_line.with_index(1) do |line, n|
      line = line.chomp
      next if line.strip.empty? || line.lstrip.start_with?("#")
      k, v = line.split(SEP, 2)
      raise Malformed, "line #{n}: no '#{SEP.strip}' separator" if v.nil?
      raise Malformed, "line #{n}: bad name #{k.inspect}" if k !~ NAME
      raise Malformed, "line #{n}: #{k} given twice" if out.key?(k)
      out[k] = v.strip
    end
    return out
  end

  # The table as text, one line per name, in name order, after the
  # comment lines given -- so that a table is written the same way
  # whoever changed it last.
  def render(hash, comment: nil)
    head = comment ? comment.lines.map { |l| "# #{l}".rstrip + "\n" }.join : ""
    return head + hash.sort.map { |k, v| "#{k}#{SEP}#{v}\n" }.join
  end

  # The table at `path`, or nil where there is none.
  def read(path)
    return nil if !File.file?(path)
    return parse(File.read(path))
  end

  # Written whole, beside its place and renamed over it, so that a
  # reader never sees half a table.
  def write(path, hash, comment: nil)
    path = path.to_s
    FileUtils.mkdir_p(File.dirname(path))
    tmp = "#{path}.#{Process.pid}.tmp"
    File.write(tmp, render(hash, comment: comment))
    File.rename(tmp, path)
  end
end
