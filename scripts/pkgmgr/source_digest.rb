# SPDX-License-Identifier: BSD-2-Clause

require 'digest'
require 'prism'

#
# Fingerprinting the CODE that builds a package.
#
# A package's build recipe is data when it can be -- a list of flags
# the helper both executes and records -- but a third of the tree does
# something a flag list cannot express: micropython builds two
# components in two directories with env deletions and OS-conditional
# arguments, host_gcc installs a specs file and verifies its own
# output. For those, the method IS the recipe, so the method is what
# gets hashed.
#
# Two properties make that usable rather than merely correct:
#
#   * it is NARROW. Only the methods a package defines itself are
#     hashed, not the file they live in, so a change to one package's
#     build does not invalidate its neighbours -- and not the base
#     class either, except for the build helpers a package actually
#     calls.
#
#   * COMMENTS DO NOT COUNT. Rewriting a comment in host_gcc's
#     109-line install method would otherwise cost a 30-minute
#     rebuild, which is the kind of price that makes people turn a
#     safety mechanism off.
#
module SourceDigest

  module_function

  # The source of every method defined directly in `klass`, comments
  # blanked out, concatenated in a stable order.
  #
  # Sorted by name rather than by position so that moving a method
  # within its file is not a change: what matters is what the code
  # says, not where it sits.
  def class_source(klass)

    file = source_file_of(klass)
    return "" if file.nil?

    defs = method_defs(file)
    names = klass.instance_methods(false).sort + \
            klass.private_instance_methods(false).sort

    return names.uniq.filter_map { |n| defs[n] }.join("\n")
  end

  # The source of one named method from a file.
  def method_source(file, name)
    return method_defs(file)[name].to_s
  end

  def digest(*parts) = Digest::SHA256.hexdigest(parts.join("\n"))

  # { :method_name => "comment-free source" } for one file.
  #
  # Parsed once per file and cached: a full status listing asks about
  # every package, and re-parsing 55 files each time would be felt.
  def method_defs(file)
    @cache ||= {}
    @cache[file] ||= parse_defs(file)
  end

  def parse_defs(file)

    return {} if !File.file?(file)

    result = Prism.parse_file(file.to_s)

    # binread, NOT read: Prism reports BYTE offsets, while String#[]=
    # with a Range addresses CHARACTERS. Any multi-byte character
    # earlier in the file -- an em-dash in a comment is enough --
    # makes the two disagree, and the offsets then run off the end:
    #
    #   String#[]=: 46419...46435 out of range (RangeError)
    #
    # Reading as bytes makes the two coordinate systems the same one.
    src = File.binread(file)

    # Blank every comment in place, so offsets stay valid and only
    # the code contributes to the hash.
    for c in result.comments do
      a, b = c.location.start_offset, c.location.end_offset
      src[a...b] = " " * (b - a)
    end

    out = {}
    collect_defs(result.value) do |node|
      body = src[node.location.start_offset...node.location.end_offset]
      out[node.name] = body.lines.map(&:rstrip).reject(&:empty?).join("\n")
    end

    return out
  end

  def collect_defs(node, &block)
    return if node.nil?
    block.call(node) if node.is_a?(Prism::DefNode)
    node.compact_child_nodes.each { |c| collect_defs(c, &block) }
  end

  # Where a class was defined. Ruby records this per method, so the
  # file is taken from any method the class defines itself.
  def source_file_of(klass)
    m = klass.instance_methods(false).first ||
        klass.private_instance_methods(false).first
    return nil if m.nil?
    loc = klass.instance_method(m).source_location
    return loc && loc[0]
  end
end
