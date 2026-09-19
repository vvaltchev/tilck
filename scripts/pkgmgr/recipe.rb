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
# ONLY Mkdir MAKES A DIRECTORY. Copy, Move, Write and Symlink require
# the parent of what they write to exist already, as cp and mv do, so
# that a mistyped destination fails instead of quietly putting the
# artifact somewhere nobody looks.
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
    field :except, []

    def run(ctx) = Recipe.transfer(ctx, :cp_r, "copy", from, to, except)
    def describe = "copy #{from} -> #{to}"
  end

  class Move < Step
    def self.tag = "move"

    field :from
    field :to
    field :except, []

    def run(ctx) = Recipe.transfer(ctx, :mv, "move", from, to, except)
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

    # Replacing a symlink is the point -- the same build run twice
    # must work. Replacing anything ELSE is not: the link often points
    # into the source tree, and rm -rf of a real directory there is
    # not a thing a recipe should be able to do by accident.
    def run(ctx)

      at = ctx.path_of(link)
      Recipe.needs_parent("symlink", link, at)

      if File.exist?(at) && !File.symlink?(at)
        raise Error, "symlink: #{link} exists and is not a symlink"
      end

      File.unlink(at) if File.symlink?(at)
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
    # Only if the file is not there: for a placeholder the build needs
    # and a tarball may or may not ship -- glycin's po/LINGUAS -- so
    # that one shipped is never overwritten with the placeholder.
    field :if_absent, false

    def run(ctx)
      at = ctx.path_of(path)
      return if if_absent && File.exist?(at)
      Recipe.needs_parent("write", path, at)
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

  # Where a symlink points, as written. What a recipe that rewrites
  # links reads before it rewrites them.
  class Readlink < Step
    def self.tag = "readlink"

    field :bind
    field :path

    def run(ctx)
      at = ctx.path_of(path)
      raise Error, "readlink: #{path} is not a symlink" if !File.symlink?(at)
      ctx.bind(bind, File.readlink(at))
    end

    def describe = "readlink $#{bind} <- #{path}"
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
  # A String pattern is expanded before it is matched -- "$INSTALL" is
  # the whole point of one, the staged path a build baked into a file
  # -- and then matched literally. A Regexp is used as written: a
  # token expands to a path, and a path is not something to splice
  # into a regular expression. Replacements are LITERAL either way:
  # \1 is a backslash and a one, not a group. Use Extract when a
  # capture is what you want.
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
        pat = Recipe.pattern_of(ctx, pattern)
        rep = ctx.expand(replacement)
        if !Recipe.matches?(text, pat)
          raise Error, "transform $#{bind}: #{pat.inspect} matches " \
                       "nothing; the rewrite would do nothing"
        end
        text = text.gsub(pat) { rep }
      end
      ctx.bind(bind, text)
    end

    def describe = "transform $#{bind} <- #{subs.length} sub(s) of #{from}"
  end

  # --- files as values -------------------------------------------------------

  #
  # A file rewritten as a named normal form of itself. busybox's make
  # leaves .config with a dated header, blank lines and the symbols in
  # Kconfig order; other/busybox.config is kept in the normal form,
  # and the build's .config is put in it too, so that the two compare
  # equal (userapps/CMakeLists.txt asks) and -C's copy of one over the
  # other is a copy of equals. The form is NAMED, never given as code:
  # a step is a value, digested by its fields.
  #
  class Normalize < Step
    def self.tag = "normalize"

    field :path
    field :form

    FORMS = {
      "kconfig" => ->(text) { Recipe.kconfig_normal_form(text) },
    }.freeze

    def check
      raise Error, "normalize: unknown form #{form.inspect}; one of " \
                   "#{FORMS.keys.join(', ')}" if !FORMS.key?(form)
    end

    def run(ctx)
      f = ctx.path_of(path)
      raise Error, "normalize: no such file #{path}" if !File.file?(f)
      File.write(f, FORMS.fetch(form).call(File.read(f)))
    end

    def describe = "normalize #{path} (#{form})"
  end

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
          pat = Recipe.pattern_of(ctx, pattern)
          next if !Recipe.matches?(text, pat)
          seen[i] = true
          rep = ctx.expand(replacement)
          text = text.gsub(pat) { rep }
        }

        File.binwrite(f, text)
      end

      seen.each_with_index { |ok, i|
        next if ok
        pat = Recipe.pattern_of(ctx, subs[i][0])
        raise Error, "substitute: #{pat.inspect} matches nothing in any " \
                     "of the #{files.length} file(s) at #{path}"
      }
    end

    def describe = "substitute #{subs.length} sub(s) in #{path}"
  end

  # --- scope, housekeeping ---------------------------------------------------

  #
  # The only way to set a directory or an environment: one structure
  # per effect, so one build has one digest rather than two spellings.
  #
  # `env_from` names an environment the RUNNER supplies rather than
  # the recipe: the stack's own toolchain, cargo's. Which one a build
  # runs in is a real difference between two builds, so the NAME is
  # identity; what it contains -- this machine's compiler paths --
  # comes from the coordinates, which are already in the install path,
  # so the VALUES are not.
  class Within < Step
    def self.tag = "within"

    field :dir, nil
    field :env, {}
    field :unset, []
    field :env_from, nil
    field :steps, []

    def check
      raise Error, "within: steps must all be steps" if
        !steps.is_a?(Array) || steps.any? { |s| !s.is_a?(Step) }
      raise Error, "within: env must be a hash" if !env.is_a?(Hash)
      raise Error, "within: unset must be an array" if !unset.is_a?(Array)
      raise Error, "within: env_from must be a symbol" if
        !env_from.nil? && !env_from.is_a?(Symbol)
    end

    def run(ctx)

      inner = ctx.scoped(dir: dir, env: env, unset: unset)

      if !File.directory?(inner.dir)
        raise Error, "within: no such directory: #{dir}"
      end

      body = -> { steps.each { |s| s.run(inner) } }
      return body.call if env_from.nil?
      return ctx.ambient(env_from, &body)
    end

    def describe
      where = [dir && "dir=#{dir}", env_from && "env_from=#{env_from}",
               !env.empty? && "env=#{env.keys.sort}",
               !unset.empty? && "unset=#{unset}"].select { |x| x }.join(" ")
      return "within #{where} (#{steps.length} step(s))"
    end
  end

  #
  # The same steps once per entry a glob matches, the entry bound to a
  # name: a cross compiler's bin/ has forty tools whose names and link
  # targets all say musl-, and forty Moves would be the same Move
  # forty times.
  #
  # BOUNDED, not a loop: the entries are what the glob matches when
  # the step starts, in sorted order, and nothing inside can add to
  # them. No condition, either -- `kind` narrows the entries to files,
  # directories or symlinks, and a step inside that has nothing to do
  # for an entry fails the build, the way it would anywhere else. A
  # glob that matches nothing fails too: a loop over nothing is a
  # typo, not a success.
  #
  class ForEach < Step
    def self.tag = "foreach"

    field :glob
    field :as
    field :kind, :any
    field :steps, []

    def check
      raise Error, "foreach: steps must all be steps" if
        !steps.is_a?(Array) || steps.any? { |s| !s.is_a?(Step) }
      raise Error, "foreach: kind must be :any, :file, :dir or :symlink" if
        ![:any, :file, :dir, :symlink].include?(kind)
    end

    def run(ctx)

      pattern = ctx.expand(glob)
      dir = ctx.dir
      found = Dir.glob(pattern, base: dir).sort.select { |e|
        Recipe.of_kind?(File.join(dir, e), kind)
      }

      if found.empty?
        raise Error, "foreach: nothing matches #{glob}" +
                     (kind == :any ? "" : " (#{kind}s only)")
      end

      for e in found do
        ctx.bind(as, e)
        for s in steps do
          s.run(ctx)
        end
      end
    ensure
      ctx.unbind(as)
    end

    def describe
      only = kind == :any ? "" : " #{kind}s"
      return "foreach $#{as} in #{glob}#{only} (#{steps.length} step(s))"
    end
  end

  #
  # Discard the build tree, keeping the install and the logs -- the
  # logs of an out-of-tree build first lifted out of the directory
  # about to go, prefixed with where they came from.
  #
  # The logs are the record of HOW a package was built, and they are
  # worth more than the space: warnings a newer compiler raises on
  # older code often mark undefined behaviour it is about to exploit,
  # and that signal is only visible there. Deleting them alongside
  # the source made the question unanswerable without a rebuild.
  #
  class Prune < Step
    def self.tag = "prune"

    # What survives. The default is a package that installs into
    # install/; one whose deliverable is a single file at the top says
    # so instead.
    field :keep, ["install", "*.log"]

    def run(ctx) = ctx.prune(keep)
    def describe = "prune (keeping #{keep.join(" ")})"
  end

  KINDS = [Run, Capture, Mkdir, Copy, Move, Remove, Symlink, Chmod, Write,
           Read, Readlink, Set, Extract, Transform, Normalize, Substitute,
           Within, ForEach, Prune].freeze

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

    # clone, not Ctx.new: a subclass carries more than these five
    # things (the package whose build this is), and the binds must
    # stay the SAME hash -- a value captured inside a scope belongs to
    # the run, not to the directory it happened to be in.
    def scoped(dir: nil, env: {}, unset: [])
      c = clone
      c.rescope(dir ? path_of(dir) : @dir, @env.merge(env), @unset | unset)
      return c
    end

    # An environment the runner supplies, by name. The base knows
    # none: a recipe that asks for one is told so rather than quietly
    # running without it.
    def ambient(name, &block)
      raise Error, "no ambient environment named #{name.inspect} here"
    end

    def expand(str)
      return str.to_s.gsub(TOKEN) { $1 == "$" ? "$" : lookup($1) }
    end

    def expand_all(list) = list.map { |e| expand(e) }

    # A token's value may be a Proc, and then it is resolved only if a
    # step actually asks: $PYTHON needs another package installed and
    # $SRC_REF needs the source extracted, both true while building
    # and neither during a staleness check.
    def lookup(name)

      return @binds[name] if @binds.key?(name)

      if @tokens.key?(name)
        v = @tokens[name]
        return (v.is_a?(Proc) ? v.call : v).to_s
      end

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

    # A ForEach's entry name lives only inside it.
    def unbind(name) = @binds.delete(name)

    def path_of(p)
      s = expand(p)
      return File.absolute_path?(s) ? s : File.join(@dir, s)
    end

    def glob(p) = Dir.glob(path_of(p)).sort

    # What the scope adds to a command's environment. A nil value is a
    # variable REMOVED for that command, which is what spawn does with
    # one -- and what `unset` means.
    def spawn_env

      env = {}
      for k in @unset do env[k] = nil end
      for k, v in @env do env[k] = expand(v.to_s) end
      return env
    end

    def run_argv(argv, log: nil, stdin: nil, capture: nil)

      # [prog, argv0], not a bare string: a one-element argv would
      # otherwise go through a shell the moment it contained a space.
      cmd = argv.length == 1 ? [[argv[0], argv[0]]] : argv
      out, err, st = Open3.capture3(spawn_env, *cmd, chdir: @dir,
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

    protected

    def rescope(dir, env, unset)
      @dir = dir
      @env = env
      @unset = unset
    end

    public

    def prune(keep = ["install", "*.log"])

      kept = ->(e) {
        keep.any? { |p| File.fnmatch?(p, e, File::FNM_DOTMATCH) }
      }

      for d in Dir.children(@root) do
        at = File.join(@root, d)
        next if kept.call(d) || !File.directory?(at)

        Dir.glob("#{at}/*.log").each { |l|
          FileUtils.mv(l, File.join(@root, "#{d}-#{File.basename(l)}"))
        }
      end

      for e in Dir.children(@root) do
        next if kept.call(e)
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

  # A kconfig .config in its normal form: the generated header and
  # every other line that names no symbol gone, blank lines gone,
  # trailing space gone, the symbol lines in one fixed order. A normal
  # form is its own normal form: the file other/busybox.config is kept
  # in it, and a built .config is put in it (Normalize).
  def kconfig_normal_form(text)
    lines = text.lines.select { |x| x.include?("CONFIG_") }
    lines = lines.map(&:rstrip)
    lines = stable_sort(lines) { |x, y| -(x.b <=> y.b) }
    return lines.join("\n") + "\n"
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

  # Every step of a recipe in order, the ones inside a Within
  # included. What an audit, a listing or a dry run walks.
  def walk(steps, &block)
    for s in steps do
      block.call(s)
      walk(s.steps, &block) if s.is_a?(Within) || s.is_a?(ForEach)
    end
  end

  # lstat, not stat: a ForEach over symlinks must see the links, not
  # what they point at, and a dangling one is still a link.
  def of_kind?(path, kind)
    case kind
    when :any     then true
    when :symlink then File.symlink?(path)
    when :file    then !File.symlink?(path) && File.file?(path)
    when :dir     then !File.symlink?(path) && File.directory?(path)
    end
  end

  # Every command a recipe runs, flattened.
  def all_argv(steps)
    out = []
    walk(steps) { |s| out.concat(s.argv) if s.is_a?(Run) }
    return out
  end

  # --- shared by more than one kind ------------------------------------------

  def matches?(text, pattern)
    return pattern.is_a?(Regexp) ? !!(text =~ pattern) : text.include?(pattern)
  end

  # A String pattern with its tokens resolved; a Regexp as written.
  def pattern_of(ctx, pattern)
    return pattern.is_a?(String) ? ctx.expand(pattern) : pattern
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
  # "." as a source means the ENTRIES of that directory, dotfiles
  # included -- what `cp -r src/. dst` does, and the only way to copy
  # a tree whose contents are not known in advance.
  def sources_of(ctx, from)

    return ctx.glob(from) if File.basename(ctx.expand(from)) != "."

    dir = ctx.path_of(from)
    return Dir.children(dir).sort.map { |e| File.join(dir, e) }
  end

  def transfer(ctx, op, who, from, to, except = [])

    srcs = sources_of(ctx, from).reject { |s|
      except.any? { |pat|
        File.fnmatch?(pat, File.basename(s), File::FNM_DOTMATCH)
      }
    }

    raise Error, "#{who}: nothing matches #{from}" if srcs.empty?
    dst = ctx.path_of(to)

    if srcs.length > 1 && !File.directory?(dst)
      raise Error, "#{who}: #{from} matches #{srcs.length} paths, so #{to} " \
                   "must be an existing directory"
    end

    Recipe.needs_parent(who, to, dst) if !File.directory?(dst)
    FileUtils.send(op, srcs.length == 1 ? srcs.first : srcs, dst)
  end

  # ONLY Mkdir makes a directory. Every other step that writes one
  # requires its parent to be there already, the way cp and mv do:
  # a step that quietly created the directory a typo named would
  # succeed at putting the file somewhere nobody looks.
  def needs_parent(who, shown, path)

    parent = File.dirname(path)
    return if File.directory?(parent)

    raise Error, "#{who}: #{shown}: #{parent} is not a directory. Only " \
                 "Mkdir creates one."
  end
end
