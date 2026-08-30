# SPDX-License-Identifier: BSD-2-Clause

require 'digest'

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
  def render(recipe:, files:, argv: nil)

    lines = ["recipe #{recipe}"]

    for path in files.sort_by(&:to_s) do
      lines << "file   #{normalize(path.to_s)} #{digest_file(path)}"
    end

    lines << "argv   #{normalize(argv)}" if argv
    return lines.join("\n") + "\n"
  end

  # Read back the comparable lines only, so that adding an
  # informational field later cannot make every install look stale.
  def comparable(dir)

    path = dir / FILE
    return nil if !File.file?(path)

    return File.read(path).lines
               .map(&:chomp)
               .select { |l| l.start_with?("recipe ", "file   ") }
               .join("\n")
  end

  def write(dir, recipe:, files:, argv: nil)
    File.write(dir / FILE, render(recipe: recipe, files: files, argv: argv))
  end
end
