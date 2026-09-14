# SPDX-License-Identifier: BSD-2-Clause

require 'digest'
require_relative 'record'

#
# What a package was built FROM, recorded beside what was built.
#
# Without this, "installed" means only "a directory with that name and
# version exists". It does not mean the artifact matches the sources,
# and the difference is not theoretical:
#
#   $ ls scripts/patches/libglycin/2.2.alpha.7/
#   0001-no-sandbox-outside-usr.diff
#   $ ./scripts/build_toolchain -s host_libglycin
#   INFO: All requested packages are already installed
#
# The patch changed the artifact's behaviour completely and pkgmgr
# still called the unpatched build current. The same shape shipped a
# gdk-pixbuf that could not decode an icon while reporting success.
#
# Recorded, not content-addressed: the path stays legible (see
# docs/plans/toolchain5.md), and staleness is DETECTED rather than
# made unreachable. That is a weaker guarantee than Nix's and a
# deliberate trade.
#
module BuildInputs

  FILE = ".build_inputs"

  #
  # Which scheme computed the `recipe` digest in a record.
  #
  #   1  the recipe was the SOURCE of the Ruby that built the package
  #   2  the recipe is the STEPS where a package declares them, and
  #      the source where it does not
  #   3  the recipe is the steps, for every package -- an empty
  #      recipe included, which under 2 was still hashed as source
  #
  # Recorded but NOT compared. A record written under 1 for a package
  # whose recipe was already a step list is still valid -- the digest
  # has not moved -- and comparing the number would call every such
  # install stale for no reason. It is read only to explain a
  # mismatch: a digest computed one way cannot disagree with one
  # computed the other, it can only be incomparable, and "rebuild
  # because your sources changed" is the wrong thing to say about
  # that.
  #
  FORMAT = 4

  # The last format whose digests this one cannot compare with.
  LAST_INCOMPARABLE = 2

  # Absolute paths are rewritten to tokens before being recorded, so
  # that the file is stable across machines and still diffable by eye.
  # The parallelism goes too: -j is a property of the machine, not of
  # what was built.
  module_function

  def normalize(text)

    out = text.to_s.dup
    out = out.gsub(TC.to_s, "$TC")
    out = out.gsub(MAIN_DIR.to_s, "$SRC")
    out = out.gsub(Dir.home, "$HOME") rescue out
    out = out.gsub(/-j\d+/, "-j$PAR")
    return out
  end

  def digest_file(path)
    return "missing" if !File.file?(path)
    return "sha256:" + Digest::SHA256.file(path.to_s).hexdigest[0, 32]
  end

  # The comparable part: everything knowable WITHOUT running a build.
  # argv is recorded too, but only for a human reading the file -- it
  # cannot be compared, because it is not known until the build runs.
  def pairs(recipe:, files:, argv: nil)
    out = [["format", FORMAT], ["recipe", recipe]]
    for path in files.sort_by(&:to_s) do
      out << ["file", "#{normalize(path.to_s)} #{digest_file(path)}"]
    end
    out << ["argv", normalize(argv)] if argv
    return out
  end

  def render(recipe:, files:, argv: nil)
    return Record.render(pairs(recipe: recipe, files: files, argv: argv))
  end

  # The record as {key => [values]}, whichever spelling wrote it: the
  # `key value` of formats 1..3 (a `file` line padded to a column),
  # or the `key: value` of 4.
  def read_any(text)
    return Record.parse(text) if text.lines.first.to_s.start_with?("format: ")
    kv = Hash.new { |h, k| h[k] = [] }
    for line in text.lines do
      k, v = line.chomp.split(" ", 2)
      kv[k] << v.to_s.strip if k
    end
    return kv
  end

  # Which scheme wrote this record. A record from before the line
  # existed is a 1, which is what it was; no record is no scheme,
  # and nil says so rather than a number.
  def format_of(dir)
    path = dir / FILE
    return nil if !path.file?
    f = read_any(File.read(path))["format"].first
    return f ? f.to_i : 1
  end

  # The lines that decide whether an install matches its sources,
  # spelled one way whichever format wrote them. Everything else in
  # the file is for a human to read.
  COMPARABLE = ["recipe", "file"].freeze

  # BOTH sides of the comparison go through this, which is what makes
  # the promise true: adding an informational field cannot make every
  # install look stale. Only the recorded side was filtered, and the
  # `format` line -- the first informational line that is always
  # written -- promptly made eight healthy installs read changed.
  def comparable_lines(text)
    kv = read_any(text)
    return COMPARABLE.flat_map { |k| kv[k].map { |v| "#{k}: #{v}" } }
                     .join("\n")
  end

  # Read back the comparable lines only.
  def comparable(dir)
    path = dir / FILE
    return nil if !File.file?(path)
    return comparable_lines(File.read(path))
  end

  def write(dir, recipe:, files:, argv: nil)
    Record.write(dir / FILE, pairs(recipe: recipe, files: files, argv: argv))
  end

  # A record of an older spelling rewritten in this one, its digests
  # kept as they are: not a new judgement, the same one in the shape
  # every record has now. Nothing for a record already in it.
  def rewrite_in_place(dir)
    path = dir / FILE
    return false if !path.file?
    text = File.read(path)
    return false if text.start_with?("format: ")
    kv = read_any(text)
    return false if kv["format"].first.to_i <= LAST_INCOMPARABLE
    out = [["format", FORMAT]]
    out << ["recipe", kv["recipe"].first] if kv["recipe"].first
    kv["file"].each { |v| out << ["file", v] }
    kv["argv"].each { |v| out << ["argv", v] }
    Record.write(path, out)
    return true
  end
end
