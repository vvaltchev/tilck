# SPDX-License-Identifier: BSD-2-Clause
#
# Two ways a build reaches outside the packages pkgmgr builds, both
# declared and neither inside a recipe:
#
#   * a SOURCE may bring extra files -- a pinned wheel -- fetched into
#     the cache beside its tarball, for the recipe to name as
#     "$CACHE/<file>". A download inside a recipe is a reach outside
#     the toolchain that the fingerprint cannot see;
#
#   * a SYSTEM DEPENDENCY may carry a token, resolved to where the
#     host keeps that package. uboot's tools want OpenSSL, and on
#     macOS a keg-only Homebrew formula is on no default path: only
#     `brew --prefix` says where. That runs when a step names the
#     token, never when the recipe is fingerprinted.
#

require_relative 'test_helper'

class TestSourceExtraFiles < Minitest::Test

  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # A module_function is its own singleton method: removing the stub
  # would remove the real one. Save it, put it back.
  def with_download_stub(stub)
    saved = Cache.method(:download_file)
    Cache.define_singleton_method(:download_file, &stub)
    yield
  ensure
    Cache.define_singleton_method(:download_file, saved)
  end

  def with_recorded_downloads
    got = []
    record = ->(url, remote, local = nil) { got << [url, remote, local]; true }
    with_download_stub(record) { yield got }
  end

  def test_extra_files_are_fetched_with_the_tarball
    src = SourceRef.new(
      name: "py", url: "https://example/py",
      tarname: ->(v) { "py-#{v}.tgz" },
      fetch_via_git: false,
      extra_files: [{ url: "https://example/wheels",
                      file: "distlib-0.3.9-py2.py3-none-any.whl" }],
    )

    with_recorded_downloads do |got|
      assert src.download(Ver("3.11.16"))
      assert_equal [["https://example/py", "py-3.11.16.tgz", "py-3.11.16.tgz"],
                    ["https://example/wheels",
                     "distlib-0.3.9-py2.py3-none-any.whl", nil]], got
    end
  end

  def test_an_extra_file_that_fails_fails_the_download
    src = SourceRef.new(name: "x", url: "https://example/x",
                        fetch_via_git: false,
                        extra_files: [{ url: "u", file: "f" }])
    with_download_stub(->(*a) { a[1] != "f" }) do
      refute src.download(Ver("1.0"))
    end
  end

  def test_the_cache_is_a_token
    with_fake_tc do
      pkg = FakePackage.new("host_p", on_host: true)
      pkgmgr.register(pkg)
      ctx = Package::BuildCtx.new(bound(pkg), Pathname.new("/x/1.0.0"))
      assert_equal "#{TC_CACHE}/some.whl", ctx.expand("$CACHE/some.whl")
    end
  end
end

class TestSystemDepPrefix < Minitest::Test

  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # A backend that answers as told, and remembers what it was asked.
  class FakeBackend < SystemPkgs::Backend
    attr_reader :asked
    def initialize(id, prefixes)
      super(id, id.to_s)
      @prefixes = prefixes
      @asked = []
    end
    def prefix_of(pkg)
      @asked << pkg
      return @prefixes[pkg]
    end
  end

  class FakeEnv < SystemDeps::Env
    def initialize(backend) = @b = backend
    def backend = @b
  end

  SSL = SystemDeps::SysDep.new(
    key: :openssl, what: "OpenSSL headers and libraries",
    pkgs: { brew: "openssl@3", apt: "libssl-dev" }, token: "openssl")

  class NeedsSsl < TestHelper::FakePackage
    def system_deps(ver = nil) = [SSL]
  end

  def with_env(backend)
    old = SystemDeps.env
    SystemDeps.env = FakeEnv.new(backend)
    yield
  ensure
    SystemDeps.env = old
  end

  def test_distro_backends_keep_it_on_the_default_path
    assert_equal "/usr", SystemPkgs::AptBackend.new.prefix_of("libssl-dev")
    assert_equal "/usr", SystemPkgs::DnfBackend.new.prefix_of("openssl-devel")
  end

  def test_brew_asks_the_formula_where_it_lives
    # self inside a define_singleton_method block is the module, so
    # the argv is recorded there and asserted here.
    asked = nil
    saved = SystemPkgs.method(:run_capture)
    SystemPkgs.define_singleton_method(:run_capture) { |argv|
      asked = argv
      [true, "/opt/homebrew/opt/openssl@3\n"]
    }
    begin
      b = SystemPkgs::BrewBackend.new
      assert_equal "/opt/homebrew/opt/openssl@3", b.prefix_of("openssl@3")
      assert_equal %w[brew --prefix openssl@3], asked
      assert_nil b.prefix_of(nil), "no name for this backend, no prefix"
    ensure
      SystemPkgs.define_singleton_method(:run_capture, saved)
    end
  end

  def test_the_token_resolves_to_the_prefix_and_only_when_named
    with_fake_tc do
      backend = FakeBackend.new(:brew, "openssl@3" => "/opt/homebrew/opt/ssl")
      with_env(backend) do
        pkg = NeedsSsl.new("host_needs_ssl", on_host: true)
        pkgmgr.register(pkg)
        ctx = Package::BuildCtx.new(bound(pkg), Pathname.new("/x/1.0.0"))

        assert_empty backend.asked, "nothing asked until a step names it"
        assert_equal "-I/opt/homebrew/opt/ssl/include",
                     ctx.expand("-I$openssl/include")
        assert_equal ["openssl@3"], backend.asked
      end
    end
  end

  # A prefix the host cannot give is refused, not expanded to "".
  def test_no_prefix_is_a_refusal_not_an_empty_string
    with_fake_tc do
      with_env(FakeBackend.new(:brew, {})) do
        pkg = NeedsSsl.new("host_needs_ssl", on_host: true)
        pkgmgr.register(pkg)
        ctx = Package::BuildCtx.new(bound(pkg), Pathname.new("/x/1.0.0"))
        err = assert_raises(Recipe::Error) { ctx.expand("$openssl/include") }
        assert_match(/OpenSSL headers and libraries was not found/,
                     err.message)
      end
    end
  end

  # A dependency without a token is a check, not a name.
  def test_a_dependency_without_a_token_is_not_a_token
    with_fake_tc do
      pkg = FakePackage.new("host_plain", on_host: true)
      pkgmgr.register(pkg)
      ctx = Package::BuildCtx.new(bound(pkg), Pathname.new("/x/1.0.0"))
      assert_raises(Recipe::Error) { ctx.expand("$openssl") }
    end
  end
end


class TestSystemDepCommandToken < Minitest::Test

  include TestHelper

  class FakeEnv < SystemDeps::Env
    def initialize(found) = @found = found
    def which(cmd) = @found[cmd]
    def backend = nil
  end

  # A command dependency's location is the command's path, found the
  # way its check finds it -- what a cross file names rustc by.
  def test_a_command_dependency_resolves_to_its_path
    env = FakeEnv.new("rustc" => "/home/x/.cargo/bin/rustc")
    assert_equal "/home/x/.cargo/bin/rustc",
                 SystemDeps::RUSTC.location(env)
    assert_equal "rustc", SystemDeps::RUSTC.token
    assert_nil SystemDeps::RUSTC.location(FakeEnv.new({}))
  end
end

class TestStackTokens < Minitest::Test

  include TestHelper

  def setup
    reset_pkgmgr!
    FakePackage.clear_log!
  end

  # The stack's compiler is a coordinate: named lazily, and refused
  # with the reason when the stack is not built rather than expanded
  # to something that is not there.
  def test_the_stack_compiler_tokens_are_lazy_and_honest
    with_fake_tc do
      pkg = FakePackage.new("host_x", on_host: true, host_tier: :stack)
      pkgmgr.register(pkg)
      ctx = Package::BuildCtx.new(bound(pkg), Pathname.new("/x/1.0.0"))
      assert_equal "-j#{BUILD_PAR}", ctx.expand("-j$PAR"),
                   "an unrelated token must not resolve the stack"
      err = assert_raises(RuntimeError) { ctx.expand("$STACK_GCC/gcc") }
      assert_match(/host toolchain is not installed/, err.message)
    end
  end
end
