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
    assert_equal Ver("14.4.0"), @pkg.default_ver,
                 "this test assumes the default is 14.4.0"

    refute_includes @pkg.version_conf_args(@pkg.default_ver),
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
      a = @pkg.stack_loader(Ver("11.5.0"))
      b = @pkg.stack_loader(Ver("14.4.0"))

      refute_equal a, b
      assert a.include?("/sysroots/#{HOST_OS}-#{HOST_ARCH.name}/gcc-11.5.0/")
      assert b.include?("/sysroots/#{HOST_OS}-#{HOST_ARCH.name}/gcc-14.4.0/")
      assert a.end_with?("/usr/lib/ld-linux-x86-64.so.2")
    end
  end

  def test_the_loader_does_not_follow_the_default_either
    with_fake_tc do
      pkgmgr.with_host_stack(Ver("16.2.0")) do
        assert @pkg.stack_loader(Ver("11.5.0"))
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
      refute_equal @pkg.default_ver,
                   @pkg.installing_ver(@pkg.staging_dir(Ver("11.5.0")))
    end
  end

  # Every supported version must produce a coherent set of answers:
  # its own stack, its own loader, and flags chosen for it.
  def test_every_supported_version_is_self_consistent
    with_fake_tc do
      for v in HostGccPackage::SUPPORTED
        assert_equal v, @pkg.stack_gcc_ver(v)
        assert @pkg.stack_loader(v).include?("gcc-#{v}")
        assert @pkg.supported_version?(v)
        assert_equal (v < Ver("14.0.0")),
                     @pkg.version_conf_args(v).include?("--disable-libsanitizer")
      end
    end
  end
end
