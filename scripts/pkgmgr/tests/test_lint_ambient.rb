# SPDX-License-Identifier: BSD-2-Clause
#
# The package manager reads its inputs through their owners, and
# nowhere else. See tests/lint/ambient.rb for what is checked and why.
#
# Three lists live here, on purpose, where a reviewer sees them:
#
#   ALLOW   the R1 readers that are supposed to exist, each with the
#           reason. An entry must still name a real method, or the
#           test fails -- an allowlist that outlives its subject is a
#           hole nobody remembers opening.
#
#   SCOPE_SETTERS  the only methods that may write a scope variable.
#
#   R2_PINNED  the partial-key comparisons the tree still has. This
#           list SHRINKS: the identity migration behind InstallSelector
#           converts them, and the pin is what stops a new one from
#           appearing while it does. When it is empty, the pin goes
#           and R2 becomes a plain "none".
#

require_relative 'test_helper'
require_relative 'lint/ambient'

class TestLintAmbient < Minitest::Test

  PKGMGR = Pathname(__dir__).parent

  ALLOW = {
    "early_logic.rb" =>
      "defines ARCH, BOARD and DEFAULT_BOARD; board_bsp there is the " \
      "global pair for the startup check, by design",
    "main.rb#set_gcc_tc_ver" =>
      "the CLI boundary: turns GCC_TC_VER and ARCH into gcc_ver",
    "main.rb#early_checks" =>
      "validates the invocation's BOARD against the BSP tree",
    "main.rb#requested_arch" =>
      "the one place -a is turned into a scope",
    "layout.rb#vars" =>
      "REPORTS the invocation's ARCH/BOARD to CMake, which compares " \
      "them against its own",
    "package_manager.rb#target_arch" => "the owner",
    "package_manager.rb#board_for"   => "the owner",
  }.freeze

  SCOPE_SETTERS = %w[
    package_manager.rb#initialize
    package_manager.rb#with_target_arch
    package_manager.rb#with_target_coords
    package_manager.rb#with_host_stack
    package_manager.rb#host_stack=
  ].freeze

  R2_PINNED = [
    "gnuefi.rb#installed?",
    "package_manager.rb#show_status_all",
    "package_manager.rb#uninstall",
  ].freeze

  # Parsed once for the class, not once per test: 80 files through
  # Prism is ~600 ms, and eight tests re-doing it was most of the
  # suite's runtime.
  def violations
    @@violations ||= AmbientLint.scan_dir(PKGMGR)
  end

  def of(rule) = violations.select { |v| v.rule == rule }

  def report(list)
    return list.map { |v| "  #{v}" }.join("\n")
  end

  # --- R1 ---------------------------------------------------------------

  def test_r1_ambient_state_is_read_only_by_its_owners
    left = AmbientLint.apply_allowlist(of(:R1), ALLOW)

    assert_empty left,
                 "these read ARCH/BOARD/HOST_VER_GCC directly. Ask " \
                 "pkgmgr.target_arch / board_for / current_host_stack, " \
                 "or add an ALLOW entry with a reason:\n#{report(left)}"
  end

  def test_the_allowlist_names_things_that_exist
    for key, reason in ALLOW do
      file, meth = key.split("#", 2)
      path = PKGMGR / file

      assert path.file?, "ALLOW names #{file}, which is gone"
      refute reason.to_s.strip.empty?, "ALLOW[#{key}] has no reason"
      next if meth.nil?

      assert_includes AmbientLint.methods_of(path), meth,
                      "ALLOW names #{key}, which no longer exists"
    end
  end

  # --- R3 ---------------------------------------------------------------

  def test_r3_scope_variables_are_written_only_by_their_scopes
    left = of(:R3).reject { |v| SCOPE_SETTERS.include?(v.where) }

    assert_empty left,
                 "a scope variable written outside its with_* method " \
                 "is a scope that can be left open:\n#{report(left)}"
  end

  def test_the_scope_setters_exist
    for key in SCOPE_SETTERS do
      file, meth = key.split("#", 2)
      assert_includes AmbientLint.methods_of(PKGMGR / file), meth,
                      "SCOPE_SETTERS names #{key}, which no longer exists"
    end
  end

  # --- R2 ---------------------------------------------------------------

  def test_r2_partial_key_comparisons_only_where_pinned
    sites = of(:R2).map(&:where).uniq.sort
    new_ones = sites - R2_PINNED

    assert_empty new_ones,
                 "a NEW comparison on part of a coordinate. Identity is " \
                 "Coords; select installs through it:\n" +
                 report(of(:R2).select { |v| new_ones.include?(v.where) })
  end

  def test_r2_pin_shrinks_but_never_lies
    sites = of(:R2).map(&:where).uniq.sort
    stale = R2_PINNED - sites

    assert_empty stale,
                 "these were converted; remove them from R2_PINNED: " \
                 "#{stale.join(', ')}"
  end

  # --- the instrument itself --------------------------------------------

  # A lint that cannot see a planted violation reports nothing, and
  # "nothing" would then read as "clean".
  def test_the_lint_sees_planted_violations
    src = <<~RUBY
      class Thing
        def a(x)
          return BOARD if x == ARCH
        end
        def b(list, cc)
          list.select { |e| e.compiler == cc }
        end
      end
    RUBY

    found = AmbientLint.scan_source(src, file: "planted.rb")
    rules = found.map(&:rule).sort

    assert_equal [:R1, :R1, :R2], rules, found.map(&:to_s).join("\n")
    assert_equal ["planted.rb#a", "planted.rb#a", "planted.rb#b"],
                 found.map(&:where)

    scope = AmbientLint.scan_source("class PackageManager\n" \
                                    "  def x = (@target_arch = 1)\nend\n",
                                    file: "package_manager.rb")
    assert_equal [:R3], scope.map(&:rule)
    assert_equal ["package_manager.rb#x"], scope.map(&:where)
  end

  # A string is not a read, and a comment is not a read. A grep would
  # flag both; "ARCH=x86" is what linux_headers passes to make.
  def test_strings_and_comments_are_not_reads
    src = "def m\n  # BOARD is not read here\n  [\"ARCH=x86\"]\nend\n"
    assert_empty AmbientLint.scan_source(src)
  end
end
