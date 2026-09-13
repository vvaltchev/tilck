# SPDX-License-Identifier: BSD-2-Clause
#
# pmrecord -- what each package's build WOULD do, recorded instead of
# run. The differential oracle behind the recipe conversion: run it
# against a worktree of the commit before a conversion and against
# the working tree after, and the two must agree line for line on
# every package the conversion did not touch, and on every COMMAND of
# the ones it did.
#
# For every command: the absolute directory it runs in, the ambient
# environments wrapped around it, its log and its argv. For every file
# operation: the operation and its paths. Machine paths are tokenised
# so the same build recorded from two trees reads the same.
#
# NOTHING here may touch the tree. The packages it walks are the real
# ones and it runs with the repository as its working directory:
# acpica writes a README, vim shells out to tar. Everything that
# writes is stubbed; a version of this that forgot File.write left a
# stray README in the repository root.
#
# A recipe (build_steps) is rendered rather than executed: there is no
# filesystem for it here, so the checks that need one -- a directory
# to enter, a file for a glob to match, a parent for a write -- are
# lifted, and only those. What runs, runs.
#
#   PKGMGR_DIR=<dir> ARCH=<arch> ruby pmrecord.rb
#
require ENV.fetch("PKGMGR_DIR") + "/main"

$out = []
$cwd = ["."]
$scopes = []

FAKE = "/INSTALL_DIR/1.2.3"

def norm(s) = s.to_s.gsub(MAIN_DIR.to_s, "$SRC").gsub(TC.to_s, "$TC")
def here = $cwd.reduce(FAKE) { |a, d| File.expand_path(d, a) }

def emit(op, *args)
  $out << [norm(here), $scopes.join(","), op,
           *args.map { |a|
             a.is_a?(Array) ? a.map { |x| norm(x) } : norm(a)
           }].inspect
end

def run_command(out, argv, env: nil)
  emit("run", out.to_s, argv.map(&:to_s))
  true
end

module Kernel
  def system(*a, **k)
    emit("system", *a.flatten.map(&:to_s))
    true
  end
end

class File
  class << self
    def write(path, text, *a, **k) = emit("write", path.to_s)
    def binwrite(path, text, *a, **k) = emit("write", path.to_s)
    def delete(*paths) = emit("delete", *paths.map(&:to_s))
    def symlink(old, new) = emit("symlink", old.to_s, new.to_s)
    def unlink(*paths) = emit("unlink", *paths.map(&:to_s))
    def chmod(mode, *paths) = emit("chmod", mode.to_s, *paths.map(&:to_s))
  end
end

module FileUtils
  class << self
    %i[mkdir_p mv cp_r cp rm_rf rm_f chmod ln_s touch].each { |m|
      define_method(m) { |*a, **k| emit(m.to_s, *a.flatten.map(&:to_s)) }
    }
    define_method(:chdir) { |d, &b|
      $cwd.push(d.to_s)
      r = b ? b.call : nil
      $cwd.pop
      r
    }
  end
end

class Package
  def source_ref_short(d) = "SRCREF"
  def with_stack_toolchain(ctx = nil, &b) = in_scope("stack_toolchain", &b)
  def in_scope(n, &b)
    $scopes.push(n)
    r = b.call
    $scopes.pop
    r
  end
end

module CargoBuild
  def with_cargo_env(&b) = in_scope("cargo", &b)
end

class PackageManager
  def python_interpreter = "PYTHON_BIN"
end

module Recipe
  class Ctx
    def glob(p) = [path_of(p)]
    def prune(keep = ["install", "*.log"])
      return emit("prune") if keep == ["install", "*.log"]
      emit("prune", *keep)
    end
  end

  class Within
    def run(ctx)
      inner = ctx.scoped(dir: dir, env: env, unset: unset)
      body = -> { steps.each { |s| s.run(inner) } }
      return body.call if env_from.nil?
      return ctx.ambient(env_from, &body)
    end
  end

  # The loop body once, over a stand-in entry.
  class ForEach
    def run(ctx)
      emit("foreach", glob, kind.to_s)
      ctx.bind(as, "<#{as}>")
      steps.each { |s| s.run(ctx) }
    ensure
      ctx.unbind(as)
    end
  end

  class Readlink
    def run(ctx) = ctx.bind(bind, "<target of #{path}>")
  end

  class Substitute
    def run(ctx) = emit("substitute", ctx.path_of(path), subs.length.to_s)
  end

  module_function
  def needs_parent(who, shown, path) = nil
  def sources_of(ctx, from) = [ctx.path_of(from)]
end

for p in pkgmgr.all_packages.sort_by(&:name) do
  $out = []
  $cwd = ["."]
  $scopes = []
  begin
    p.install_impl_internal(Pathname.new(FAKE))
  rescue Exception => e
    $out << ["ERROR", e.class.to_s].inspect
  end
  next if $out.empty?
  puts "### #{p.name}"
  $out.each { |o| puts "  " + o }
end
