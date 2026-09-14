# SPDX-License-Identifier: BSD-2-Clause
#
# host_gcc's version-dependent decisions.
#
# Each of these was wrong once, in the same way: it asked about
# default_ver instead of the version being installed. With
# HOST_VER_GCC=14.4.0 that meant --disable-libsanitizer was never
# applied to anything, and the three compilers it was added for failed
# on crypt.h exactly as before — the flag had never once been used.
#
# They were only reachable by building a compiler, which is why the
# mistake survived. They are functions of an explicit version now, so
# these tests can ask them directly.
#

require_relative 'test_helper'
require_relative '../host_gcc'

class TestHostGccVersionDecisions < Minitest::Test
  include TestHelper

  def setup
    reset_pkgmgr!
    @pkg = HostGccPackage.new
    pkgmgr.register(@pkg)
  end

  # The bug, stated as a test: the answer must follow the version
  # passed in, and must NOT follow the default.
  def test_libsanitizer_is_disabled_for_versions_that_need_it
    for v in ["11.5.0", "12.5.0", "13.4.0"]
      assert_includes @pkg.version_conf_args(Ver(v)),
                      "--disable-libsanitizer",
                      "gcc #{v} needs it: glibc >= 2.39 has no crypt.h"
    end
  end

  def test_libsanitizer_is_kept_for_versions_that_build_with_it
    for v in ["14.0.0", "14.4.0", "15.3.0", "16.2.0"]
      refute_includes @pkg.version_conf_args(Ver(v)),
                      "--disable-libsanitizer", "gcc #{v}"
    end
  end

  # The regression itself: with the default at 14.4.0, asking about
  # 11.5.0 must still answer about 11.5.0.
  def test_the_answer_does_not_follow_the_default
    assert_equal Ver("14.4.0"), bound(@pkg).default_ver,
                 "this test assumes the default is 14.4.0"

    refute_includes @pkg.version_conf_args(bound(@pkg).default_ver),
                    "--disable-libsanitizer"
    assert_includes @pkg.version_conf_args(Ver("11.5.0")),
                    "--disable-libsanitizer"
  end

  def test_supported_versions_are_recognised
    for v in ["11.5.0", "12.5.0", "13.4.0", "14.4.0", "15.3.0", "16.2.0"]
      assert @pkg.supported_version?(Ver(v)), v
    end
  end

  # The same shape of bug: this check validated default_ver, so a
  # supported default would have waved through an unsupported request.
  def test_unsupported_versions_are_rejected
    for v in ["9.9.9", "13.0.0", "99.0.0"]
      refute @pkg.supported_version?(Ver(v)), v
    end
  end

  # stack_loader exists because the expression it replaces was copied
  # to three sites, and one copy referenced a local belonging to
  # another method — raising NameError rather than returning false,
  # which aborted five unrelated builds.
  def test_the_loader_belongs_to_the_stack_it_is_asked_about
    with_fake_tc do
      a = bound(@pkg).stack_loader(Ver("11.5.0"))
      b = bound(@pkg).stack_loader(Ver("14.4.0"))

      refute_equal a, b
      assert a.include?("/any/gcc-11.5.0/sysroot/")
      assert b.include?("/any/gcc-14.4.0/sysroot/")
      assert a.end_with?("/usr/lib/ld-linux-x86-64.so.2")
    end
  end

  def test_the_loader_does_not_follow_the_default_either
    with_fake_tc do
      with_host_stack(Ver("16.2.0")) do
        assert bound(@pkg).stack_loader(Ver("11.5.0"))
                   .include?("gcc-11.5.0"),
               "stack_loader must answer about its argument"
      end
    end
  end

  # A compiler belongs to its own stack, whatever the default says.
  def test_stack_is_the_version_being_installed
    with_fake_tc do
      assert_equal Ver("11.5.0"), @pkg.stack_gcc_ver(Ver("11.5.0"))
      assert_equal Ver("16.2.0"), @pkg.stack_gcc_ver(Ver("16.2.0"))
    end
  end

  # installing_ver is how the install path learns which version it is
  # building, since install_impl_internal is handed only a directory.
  def test_the_installing_version_comes_from_the_staging_path
    with_fake_tc do
      assert_equal Ver("11.5.0"),
                   @pkg.installing_ver(@pkg.staging_dir(Ver("11.5.0")))
      refute_equal bound(@pkg).default_ver,
                   @pkg.installing_ver(@pkg.staging_dir(Ver("11.5.0")))
    end
  end

  # Every supported version must produce a coherent set of answers:
  # its own stack, its own loader, and flags chosen for it.
  def test_every_supported_version_is_self_consistent
    with_fake_tc do
      for v in HostGccPackage::SUPPORTED
        assert_equal v, @pkg.stack_gcc_ver(v)
        assert bound(@pkg).stack_loader(v).include?("gcc-#{v}")
        assert @pkg.supported_version?(v)
        assert_equal (v < Ver("14.0.0")),
                     @pkg.version_conf_args(v).include?("--disable-libsanitizer")
      end
    end
  end
end


#
# The recipe itself, now that it is data: which version gets which
# flags, what the prerequisites are named by, and the specs rewrite
# exercised on a real -dumpspecs shape without a compiler to hand.
#
class TestHostGccRecipe < Minitest::Test

  include TestHelper
  include Recipe::DSL

  def setup
    reset_pkgmgr!
    @pkg = HostGccPackage.new
    pkgmgr.register(@pkg)
  end

  def configure_argv(ver)
    Recipe.walk(bound(@pkg).build_steps(ver)) { |s|
      return s.argv if s.is_a?(Recipe::Run) && s.log == "configure.log"
    }
  end

  def test_the_recipe_follows_the_version_not_the_default
    with_fake_tc do
      assert_includes configure_argv(Ver("11.5.0")), "--disable-libsanitizer"
      refute_includes configure_argv(Ver("14.4.0")), "--disable-libsanitizer"
    end
  end

  def test_the_prerequisites_and_binutils_are_named_by_token
    with_fake_tc do
      argv = configure_argv(Ver("14.4.0"))
      assert_includes argv, "--with-gmp=$host_gmp/install"
      assert_includes argv, "--with-isl=$host_isl/install"
      assert_includes argv, "--with-as=$host_binutils/install/bin/as"
      assert_includes argv, "--with-sysroot=$STACK_SYSROOT"
      refute argv.any? { |a| a.include?(TC.to_s) }, "a path leaked in"
    end
  end

  # A compiler is configured against the stack it DEFINES, which is
  # not the sysroot at its own :distro coordinates. The same token,
  # asked through a staging directory for 11.5.0 and for 14.4.0,
  # names two different sysroots -- and $SYSROOT names neither.
  def test_the_stack_sysroot_is_the_stack_the_compiler_defines
    with_fake_tc do
      a = Package::BuildCtx.new(bound(@pkg), @pkg.staging_dir(Ver("11.5.0")))
      b = Package::BuildCtx.new(bound(@pkg), @pkg.staging_dir(Ver("14.4.0")))
      assert_includes a.expand("$STACK_SYSROOT"), "gcc-11.5.0"
      assert_includes b.expand("$STACK_SYSROOT"), "gcc-14.4.0"
      refute_includes a.expand("$SYSROOT"), "gcc-11.5.0"
    end
  end

  # ...while for an ordinary stack package the two are one directory.
  def test_a_stack_packages_two_sysroots_are_the_same_one
    with_fake_tc do
      p = FakePackage.new("host_lib", on_host: true, host_tier: :stack)
      pkgmgr.register(p)
      ctx = Package::BuildCtx.new(bound(p), p.staging_dir(Ver("1.0.0")))
      assert_equal bound(p).stack_sysroot.to_s, ctx.expand("$SYSROOT")
      assert_equal ctx.expand("$SYSROOT"), ctx.expand("$STACK_SYSROOT")
    end
  end

  # The specs rewrite, on the shape gcc -dumpspecs actually prints:
  # the interpreter replaced, the rpath appended to the *link: line,
  # everything else untouched.
  SPECS = <<~SPECS
    *asm:
    %{m32:--32}

    *link:
    %{!static:--eh-frame-hdr} -m elf_x86_64 %{!shared: %{!static: -dynamic-linker /lib64/ld-linux-x86-64.so.2}}

    *lib:
    %{pthread:-lpthread} -lc
  SPECS

  def rewrite_steps
    all = []
    Recipe.walk(bound(@pkg).build_steps(Ver("14.4.0"))) { |s| all << s }
    return all.select { |s|
      (s.is_a?(Recipe::Extract) && s.bind == "link_line") ||
      (s.is_a?(Recipe::Transform) && s.bind == "specs")
    }
  end

  def test_the_specs_rewrite_replaces_the_loader_and_adds_the_rpath
    Dir.mktmpdir do |root|
      c = Recipe::Ctx.new(root: root,
                          tokens: { "STACK_SYSROOT" => "/tc/sys" })
      Recipe.run([Set(bind: "specs", value: SPECS), *rewrite_steps], c)
      out = c.bound("specs")

      refute_includes out, "/lib64/ld-linux-x86-64.so.2"
      assert_includes out,
                      "-dynamic-linker /tc/sys/usr/lib/ld-linux-x86-64.so.2"
      rpath = "%{!static:-rpath /tc/sys/usr/lib --disable-new-dtags}"
      assert_match(/^\*link:\n.*-m elf_x86_64.* #{Regexp.escape(rpath)}$/,
                   out)
      assert_includes out, "*asm:\n%{m32:--32}", "the other sections untouched"
      assert_includes out, "*lib:\n%{pthread:-lpthread} -lc"
    end
  end

  # ...and each rewrite must match, or the install stops.
  def test_a_spec_without_the_system_loader_stops_the_install
    Dir.mktmpdir do |root|
      c = Recipe::Ctx.new(root: root,
                          tokens: { "STACK_SYSROOT" => "/tc/sys" })
      moved = SPECS.sub("/lib64/ld-linux-x86-64.so.2", "/elsewhere/ld.so")
      err = assert_raises(Recipe::Error) {
        Recipe.run([Set(bind: "specs", value: moved), *rewrite_steps], c)
      }
      assert_match(/ld-linux-x86-64.so.2.*matches nothing/m, err.message)
    end
  end
end
