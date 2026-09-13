# SPDX-License-Identifier: BSD-2-Clause

require 'digest'
require 'fileutils'
require 'open3'

#
# THE RECIPE: what builds a package, as DATA.
#
# A recipe is an ordered list of Step objects, and a step is defined by
# the FIELDS it declares -- those fields are the whole of what it is:
#
#   Run(argv: ["make", "-j$PAR"], log: "build.log")
#
# Three things read that declaration, and only one of them is written
# per kind:
#
#   * the DIGEST is generic. One algorithm, the same for every kind,
#     over the declared fields. No step writes a hash method, so what a
#     step IS can never drift from what it DOES.
#
#   * `run` is per-kind code. It is trusted and never hashed, the way
#     `cp` and the compiler are trusted.
#
#   * `describe` has a generic default and may be overridden: it is
#     only what -d prints.
#
# WHY NOT HASH THE SOURCE
#
# The scheme this replaces hashed the TEXT of the Ruby method that
# built a package, which meant parsing Ruby to find that method's byte
# range and blank its comments -- a parser dependency for a question
# data answers directly. It also hashed the wrong thing: rewriting a
# comment in a 109-line install method cost a 30-minute rebuild, while
# the helper that method called could change without anyone noticing.
#
# STEPS BUILD; POSTCONDITIONS CHECK
#
# Only what produces the artifact belongs here. A check that the result
# is USABLE -- the installed gcc produces portable binaries -- installs
# nothing, so it is a postcondition of the package and is not hashed.
# Tightening a check must never invalidate an artifact.
#
# THE EVOLUTION CONTRACT
#
# Identity is the name-keyed map of the fields that are NOT at their
# default, so the model can grow for years without rebuilding the
# world. These cost nothing -- no digest anywhere changes:
#
#   * adding a new optional field to an existing kind;
#   * adding a whole new kind;
#   * renaming the Ruby class (the tag is an explicit string);
#   * reordering field declarations (the map is name-sorted);
#   * adding a new $TOKEN.
#
# These break every digest, and are therefore deliberate: renaming a
# field, changing a field's shipped default, changing what a kind DOES
# (that is what FORMAT is for), and changing the canonical encoding
# (a golden test guards it).
#
# FIELD NAMES AND SHIPPED DEFAULTS ARE THE CONTRACT.
#
module Recipe

  # The version of the step model itself, recorded beside each install
  # and compared BEFORE the digest, so that "built by an older model"
  # reports as exactly that instead of masquerading as a recipe change.
  # Bump it when a kind's MEANING changes -- when Copy stops being
  # recursive, never when Copy merely gains a field.
  FORMAT = 1

  class Error < StandardError; end

  # The default of a field that has none: it is never equal to a value
  # a caller can pass, so a required field is always present in the
  # identity map and its absence is caught at construction.
  REQUIRED = Object.new
  def REQUIRED.inspect = "<required>"
  REQUIRED.freeze

  #
  # The base class. Everything a step is, is here; a kind adds a tag,
  # its fields, and what it does.
  #
  class Step

    # Where this step's output goes. Cosmetic: moving a build from
    # build.log to make.log changes nothing about what gets built, so
    # the log is NEVER part of the identity.
    attr_reader :log

    # Declare an identity field and its default. Generates the reader,
    # and -- with the initializer below -- makes the declared fields
    # the whole of the object's state, so no hidden value can affect
    # `run` without also being in the digest.
    def self.field(name, default = REQUIRED)
      fields[name] = default
      attr_reader(name)
    end

    def self.fields
      @fields ||= superclass.respond_to?(:fields) ? superclass.fields.dup : {}
    end

    # A short, stable string. Explicit, and NOT the class name: the
    # class may be renamed without rebuilding anything.
    def self.tag
      raise Error, "#{self}: a step kind must declare a tag"
    end

    def initialize(log: nil, **kw)

      unknown = kw.keys - self.class.fields.keys

      if !unknown.empty?
        raise Error, "#{self.class.tag}: no such field(s): " +
                     unknown.join(", ")
      end

      @log = log

      for name, default in self.class.fields do
        v = kw.fetch(name, default)

        if v.equal?(REQUIRED)
          raise Error, "#{self.class.tag}: #{name}: is required"
        end

        instance_variable_set("@#{name}", v)
      end

      check
      freeze
    end

    # Normalise and validate, before the object is frozen. A kind that
    # accepts two spellings of one thing collapses them here, so that
    # the two also hash the same.
    def check; end

    # THE IDENTITY: the fields that are not at their default, keyed by
    # name. A defaulted field contributes nothing at all, which is what
    # makes a new optional field free.
    def digest_map
      return self.class.fields.filter_map { |name, default|
        v = send(name)
        next if !default.equal?(REQUIRED) && v == default
        [name.to_s, v]
      }.to_h
    end

    def run(ctx)
      raise Error, "#{self.class.tag}: run is not implemented"
    end

    def describe
      body = digest_map.map { |k, v| "#{k}=#{v.inspect}" }.join(" ")
      return [self.class.tag, body].reject(&:empty?).join(" ")
    end

    def to_s = describe

    def ==(other)
      return other.is_a?(Step) && other.class == self.class &&
             other.digest_map == digest_map
    end

    alias eql? ==
    def hash = [self.class, digest_map].hash
  end

  # --- process ---------------------------------------------------------------

  #
  # A program. The answer whenever a program does the job -- tar, gzip,
  # strip -- so that the vocabulary does not grow into a shell.
  #
  class Run < Step
    def self.tag = "run"

    field :argv
    field :stdin, nil
    field :status, 0

    def check
      raise Error, "run: argv must be a non-empty array" if
        !argv.is_a?(Array) || argv.empty?
    end

    def run(ctx)
      st, = ctx.run_argv(ctx.expand_all(argv), log: log,
                         stdin: stdin && ctx.expand(stdin))
      return if st == status
      raise Error, "#{argv.first}: exit status #{st}, expected #{status}"
    end

    def describe = "run #{argv.join(" ")}"
  end

  #
  # A program, and what it printed, bound to a name. The one way a step
  # learns something that is not knowable until the build has run.
  #
  class Capture < Step
    def self.tag = "capture"

    field :bind
    field :argv
    field :stream, :stdout
    field :status, 0

    def check
      raise Error, "capture: argv must be a non-empty array" if
        !argv.is_a?(Array) || argv.empty?
      raise Error, "capture: stream must be :stdout, :stderr or :both" if
        ![:stdout, :stderr, :both].include?(stream)
    end

    def run(ctx)
      st, out = ctx.run_argv(ctx.expand_all(argv), log: log,
                             capture: stream)
      if st != status
        raise Error, "#{argv.first}: exit status #{st}, expected #{status}"
      end
      ctx.bind(bind, out)
    end

    def describe = "capture $#{bind} <- #{argv.join(" ")}"
  end

  # --- filesystem ------------------------------------------------------------

  class Mkdir < Step
    def self.tag = "mkdir"

    field :path

    def run(ctx) = FileUtils.mkdir_p(ctx.path_of(path))
    def describe = "mkdir #{path}"
  end

  #
  # Ruby's recursive copy, not /bin/cp: the semantics are the same on
  # macOS and FreeBSD, which `cp`'s are not.
  #
  class Copy < Step
    def self.tag = "copy"

    field :from
    field :to

    def run(ctx) = Recipe.transfer(ctx, :cp_r, "copy", from, to)
    def describe = "copy #{from} -> #{to}"
  end

  class Move < Step
    def self.tag = "move"

    field :from
    field :to

    def run(ctx) = Recipe.transfer(ctx, :mv, "move", from, to)
    def describe = "move #{from} -> #{to}"
  end

  #
  # Idempotent, on purpose: `rm -rf` semantics mean a recipe never needs
  # to ask whether something exists, which is what keeps a conditional
  # out of the model.
  #
  class Remove < Step
    def self.tag = "remove"

    field :paths

    def check
      @paths = Array(@paths)
      raise Error, "remove: paths must not be empty" if @paths.empty?
    end

    def run(ctx)
      for p in paths do
        FileUtils.rm_rf(ctx.glob(p))
      end
    end

    def describe = "remove #{paths.join(" ")}"
  end

  class Symlink < Step
    def self.tag = "symlink"

    field :target
    field :link

    def run(ctx)
      at = ctx.path_of(link)
      FileUtils.rm_rf(at)
      FileUtils.mkdir_p(File.dirname(at))
      File.symlink(ctx.expand(target), at)
    end

    def describe = "symlink #{link} -> #{target}"
  end

  class Chmod < Step
    def self.tag = "chmod"

    field :path
    field :mode

    def check
      raise Error, "chmod: mode must be an Integer (0755, not \"755\")" if
        !mode.is_a?(Integer)
    end

    def run(ctx)
      found = ctx.glob(path)
      raise Error, "chmod: nothing matches #{path}" if found.empty?
      FileUtils.chmod(mode, found)
    end

    def describe = format("chmod %04o %s", mode, path)
  end

  #
  # A file with literal content. The text is expanded, so a generated
  # wrapper naming another package's interpreter is written as
  # "$PYTHON" and stays machine-independent in the digest.
  #
  class Write < Step
    def self.tag = "write"

    field :path
    field :text

    def run(ctx)
      at = ctx.path_of(path)
      FileUtils.mkdir_p(File.dirname(at))
      File.write(at, ctx.expand(text))
    end

    def describe = "write #{path}"
  end

  class Read < Step
    def self.tag = "read"

    field :bind
    field :path

    def run(ctx)
      at = ctx.path_of(path)
      raise Error, "read: no such file: #{path}" if !File.file?(at)
      ctx.bind(bind, File.read(at))
    end

    def describe = "read $#{bind} <- #{path}"
  end

  # --- values ----------------------------------------------------------------

  class Set < Step
    def self.tag = "set"

    field :bind
    field :value

    def run(ctx) = ctx.bind(bind, ctx.expand(value))
    def describe = "set $#{bind} = #{value}"
  end

  #
  # A regex group of a value. No match FAILS: a rewrite that silently
  # does nothing is the bug this whole scheme exists to catch.
  #
  class Extract < Step
    def self.tag = "extract"

    field :bind
    field :from
    field :pattern
    field :group, 1

    def run(ctx)
      text = ctx.expand(from)
      m = Regexp.new(pattern).match(text)
      if m.nil?
        raise Error, "extract $#{bind}: #{pattern.inspect} matches " \
                     "nothing in #{from}"
      end
      ctx.bind(bind, m[group])
    end

    def describe = "extract $#{bind} <- #{pattern.inspect} of #{from}"
  end

  #
  # A value rewritten. Each substitution must match at least once, for
  # the same reason Extract fails: the interesting failure is the one
  # that changes nothing and reports success.
  #
  # Replacements are LITERAL -- \1 is a backslash and a one, not a
  # group. Use Extract when a capture is what you want.
  #
  class Transform < Step
    def self.tag = "transform"

    field :bind
    field :from
    field :subs

    def check = Recipe.check_subs("transform", subs)

    def run(ctx)
      text = ctx.expand(from)
      for pattern, replacement in subs do
        rep = ctx.expand(replacement)
        if !Recipe.matches?(text, pattern)
          raise Error, "transform $#{bind}: #{pattern.inspect} matches " \
                       "nothing; the rewrite would do nothing"
        end
        text = text.gsub(pattern) { rep }
      end
      ctx.bind(bind, text)
    end

    def describe = "transform $#{bind} <- #{subs.length} sub(s) of #{from}"
  end

  # --- files as values -------------------------------------------------------

  #
  # A file rewritten in place, across every file the path matches. A
  # substitution that matches in NO file fails -- in some but not all
  # is normal, and passes.
  #
  class Substitute < Step
    def self.tag = "substitute"

    field :path
    field :subs

    def check = Recipe.check_subs("substitute", subs)

    def run(ctx)

      files = ctx.glob(path).select { |f| File.file?(f) }
      raise Error, "substitute: nothing matches #{path}" if files.empty?
      seen = Array.new(subs.length, false)

      for f in files do
        text = File.binread(f)

        subs.each_with_index { |(pattern, replacement), i|
          next if !Recipe.matches?(text, pattern)
          seen[i] = true
          rep = ctx.expand(replacement)
          text = text.gsub(pattern) { rep }
        }

        File.binwrite(f, text)
      end

      seen.each_with_index { |ok, i|
        next if ok
        raise Error, "substitute: #{subs[i][0].inspect} matches nothing " \
                     "in any of the #{files.length} file(s) at #{path}"
      }
    end

    def describe = "substitute #{subs.length} sub(s) in #{path}"
  end

  # --- scope, housekeeping ---------------------------------------------------

  #
  # The only way to set a directory or an environment: one structure
  # per effect, so one build has one digest rather than two spellings.
  #
  class Within < Step
    def self.tag = "within"

    field :dir, nil
    field :env, {}
    field :unset, []
    field :steps, []

    def check
      raise Error, "within: steps must all be steps" if
        !steps.is_a?(Array) || steps.any? { |s| !s.is_a?(Step) }
      raise Error, "within: env must be a hash" if !env.is_a?(Hash)
      raise Error, "within: unset must be an array" if !unset.is_a?(Array)
    end

    def run(ctx)
      inner = ctx.scoped(dir: dir, env: env, unset: unset)
      if !File.directory?(inner.dir)
        raise Error, "within: no such directory: #{dir}"
      end
      for s in steps do
        s.run(inner)
      end
    end

    def describe
      where = [dir && "dir=#{dir}", !env.empty? && "env=#{env.keys.sort}",
               !unset.empty? && "unset=#{unset}"].select { |x| x }.join(" ")
      return "within #{where} (#{steps.length} step(s))"
    end
  end

  #
  # Discard the build tree, keeping the install and the logs -- the
  # logs of an out-of-tree build first lifted out of the directory
  # about to go, prefixed with where they came from.
  #
  class Prune < Step
    def self.tag = "prune"

    def run(ctx) = ctx.prune
    def describe = "prune"
  end

  KINDS = [Run, Capture, Mkdir, Copy, Move, Remove, Symlink, Chmod, Write,
           Read, Set, Extract, Transform, Substitute, Within, Prune].freeze

  # The constructors a recipe is written with, generated from KINDS so
  # that a new kind is usable the moment it is defined and the two can
  # never drift:
  #
  #   Run(argv: [...])   ==   Recipe::Run.new(argv: [...])
  #
  module DSL; end

  KINDS.each { |k|
    DSL.send(:define_method, k.name.split("::").last) { |**kw| k.new(**kw) }
  }

  #
  # WHERE A RECIPE RUNS: the directory, the environment, the token
  # namespace, and the values steps bind into it.
  #
  # The default run_argv buffers a command's whole output, which is
  # right for a test and wrong for a 30-minute build: the package
  # manager overrides it to stream through its own run_command.
  #
  class Ctx

    attr_reader :root, :dir, :tokens

    # $NAME, and $$ for a literal dollar. Deliberately NOT ${NAME}:
    # ${prefix} and ${pcfiledir} are pkg-config's own syntax and must
    # survive a rewrite untouched.
    TOKEN = /\$(\$|[A-Za-z_][A-Za-z0-9_]*)/

    def initialize(root:, tokens: {}, dir: nil, env: {}, unset: [],
                   binds: nil)
      @root = File.expand_path(root.to_s)
      @dir = dir ? File.expand_path(dir.to_s, @root) : @root
      @tokens = tokens.transform_keys(&:to_s)
      @env = env
      @unset = unset
      @binds = binds || {}      # shared with every scope of one run
    end

    def scoped(dir: nil, env: {}, unset: [])
      return Ctx.new(root: @root, tokens: @tokens,
                     dir: dir ? path_of(dir) : @dir,
                     env: @env.merge(env), unset: @unset | unset,
                     binds: @binds)
    end

    def expand(str)
      return str.to_s.gsub(TOKEN) { $1 == "$" ? "$" : lookup($1) }
    end

    def expand_all(list) = list.map { |e| expand(e) }

    def lookup(name)
      return @binds[name] if @binds.key?(name)
      return @tokens[name].to_s if @tokens.key?(name)
      raise Error, "unknown token $#{name}: not a builtin, and no step " \
                   "before this one binds it"
    end

    def bind(name, value)
      if @tokens.key?(name)
        raise Error, "$#{name} is a builtin token; a step may not bind it"
      end
      @binds[name] = value.to_s
    end

    def bound(name) = @binds[name]

    def path_of(p)
      s = expand(p)
      return File.absolute_path?(s) ? s : File.join(@dir, s)
    end

    def glob(p) = Dir.glob(path_of(p)).sort

    def run_argv(argv, log: nil, stdin: nil, capture: nil)

      env = {}
      for k in @unset do env[k] = nil end
      for k, v in @env do env[k] = expand(v.to_s) end

      # [prog, argv0], not a bare string: a one-element argv would
      # otherwise go through a shell the moment it contained a space.
      cmd = argv.length == 1 ? [[argv[0], argv[0]]] : argv
      out, err, st = Open3.capture3(env, *cmd, chdir: @dir,
                                    stdin_data: stdin.to_s)
      append_log(log, out + err)

      text = case capture
             when :stderr then err
             when :both   then out + err
             else              out
             end

      return [st.exitstatus, text]
    end

    def append_log(log, text)
      return if log.nil?
      File.open(File.join(@dir, expand(log)), "a") { |f| f.write(text) }
    end

    def prune

      for d in Dir.children(@root) do
        at = File.join(@root, d)
        next if d == "install" || !File.directory?(at)

        Dir.glob("#{at}/*.log").each { |l|
          FileUtils.mv(l, File.join(@root, "#{d}-#{File.basename(l)}"))
        }
      end

      for e in Dir.children(@root) do
        next if e == "install" || e.end_with?(".log")
        FileUtils.rm_rf(File.join(@root, e))
      end
    end
  end

  module_function

  #
  # THE CANONICAL ENCODING: one unambiguous byte string per value.
  #
  # Every piece is typed and length-prefixed, so nothing can be read
  # two ways: ["a", "b"] and ["ab"] cannot collide, an array says how
  # many elements it has, and a hash is emitted in key order. That is
  # what makes the digest a function of the recipe and nothing else.
  #
  # It refuses what it does not know rather than calling to_s on it: a
  # Pathname stringifies to an absolute path, which would put this
  # machine's toolchain directory into the identity of the build.
  #
  def canon(x)

    case x
    when nil     then "n;"
    when true    then "t;"
    when false   then "f;"
    when Integer then "i#{x};"
    when Symbol  then s = x.to_s.b; "y#{s.bytesize}:#{s}"
    when String  then s = x.b;      "s#{s.bytesize}:#{s}"
    when Array   then "a#{x.length}:" + x.map { |e| canon(e) }.join
    when Regexp  then "x" + canon(x.source) + canon(x.options)
    when Step    then "S" + canon(x.class.tag) + canon(x.digest_map)
    when Hash
      pairs = x.map { |k, v| [canon(k), canon(v)] }.sort_by(&:first)
      "h#{x.size}:" + pairs.map(&:join).join
    else
      raise Error, "canon: cannot hash a #{x.class}: #{x.inspect}. Use a " \
                   "token for a path, or a String for anything else."
    end
  end

  # The recipe's identity. FORMAT is NOT mixed in: it is recorded
  # beside this, so that a model change reports as a model change.
  def digest(steps)
    return "sha256:" + Digest::SHA256.hexdigest(canon(steps))[0, 32]
  end

  def run(steps, ctx)
    for s in steps do
      s.run(ctx)
    end
  end

  # --- shared by more than one kind ------------------------------------------

  def matches?(text, pattern)
    return pattern.is_a?(Regexp) ? !!(text =~ pattern) : text.include?(pattern)
  end

  def check_subs(who, subs)

    ok = subs.is_a?(Array) && subs.all? { |s|
      s.is_a?(Array) && s.length == 2 &&
        (s[0].is_a?(String) || s[0].is_a?(Regexp)) && s[1].is_a?(String)
    }

    return if ok
    raise Error, "#{who}: subs must be [[pattern, replacement], ...], " \
                 "each pattern a String or Regexp and each replacement " \
                 "a String"
  end

  # Copy and Move differ only in the FileUtils call: both take a glob,
  # both refuse to match nothing (a typo that copies nothing is not a
  # thing anyone wants to succeed), and both require an existing
  # directory as the destination when the glob matched more than one.
  def transfer(ctx, op, who, from, to)

    srcs = ctx.glob(from)
    raise Error, "#{who}: nothing matches #{from}" if srcs.empty?
    dst = ctx.path_of(to)

    if srcs.length > 1 && !File.directory?(dst)
      raise Error, "#{who}: #{from} matches #{srcs.length} paths, so #{to} " \
                   "must be an existing directory"
    end

    FileUtils.mkdir_p(File.dirname(dst)) if !File.directory?(dst)
    FileUtils.send(op, srcs.length == 1 ? srcs.first : srcs, dst)
  end
end
