#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import, bad-indentation, wildcard-import
# pylint: disable=missing-function-docstring, missing-class-docstring
# pylint: disable=invalid-name, broad-except, consider-using-f-string

import os
import sys
import argparse
import subprocess

from pathlib import Path

from . import parser as _parser_mod
from . import reporter
from . import extents
from . import tokens as _tokens_mod
from . import aggregator as _agg
from . import config as _config
from .rules import ALL_RULES, RULES_BY_ID
from .rules.base import (
   CheckContext, detect_multi_include, detect_c_dot_h
)


def collect_files(args, repo_root: Path) -> list:

   files = []
   root = repo_root if repo_root is not None else Path.cwd()

   for f in args.files:

      p = Path(f)

      if not p.is_absolute():
         p = root / f

      files.append(p)

   if args.since:

      try:

         out = subprocess.check_output(
            ['git', 'diff', '--name-only', args.since, 'HEAD'],
            cwd=str(root),
            stderr=subprocess.DEVNULL,
            text=True
         )

      except subprocess.CalledProcessError:
         out = ''

      for line in out.split('\n'):

         line = line.strip()

         if not line:
            continue

         if line.endswith('.c') or line.endswith('.h'):

            p = root / line

            if p.exists():
               files.append(p)

   if args.all:

      try:

         out = subprocess.check_output(
            ['git', 'ls-files', '*.c', '*.h'],
            cwd=str(root),
            stderr=subprocess.DEVNULL,
            text=True
         )

      except subprocess.CalledProcessError:
         out = ''

      # The tool's own bad-fixture directory contains intentionally
      # broken code -- it would generate spurious diagnostics in a
      # corpus sweep. Exclude it. This is the tool ignoring its own
      # test data, not the tool taking a stance on the codebase.
      own_fixtures = str(
         Path(__file__).resolve().parent / 'tests' / 'fixtures'
      )

      for line in out.split('\n'):

         line = line.strip()

         if not line:
            continue

         p = root / line

         if not p.exists():
            continue

         if str(p.resolve()).startswith(own_fixtures):
            continue

         files.append(p)

   return files


def build_context(file_path: Path,
                  parser_obj,
                  need_tu: bool,
                  need_comments: bool) -> CheckContext:

   source_bytes = file_path.read_bytes()

   try:
      source_text = source_bytes.decode('utf-8')
   except UnicodeDecodeError:
      source_text = source_bytes.decode('latin-1')

   lines = source_text.split('\n')

   is_header = file_path.suffix == '.h'
   is_cpp = file_path.suffix in ('.cpp', '.hpp', '.cc')

   ctx = CheckContext(
      file_path=file_path,
      source_bytes=source_bytes,
      source_text=source_text,
      lines=lines,
      is_header=is_header,
      is_cpp=is_cpp,
      is_c_dot_h=detect_c_dot_h(file_path),
      is_multi_include=detect_multi_include(lines),
   )

   if need_tu and parser_obj is not None:

      tu = parser_obj.parse(file_path)

      if tu is not None:
         ctx.tu = tu
         ctx.extents = extents.walk_main_file(tu, file_path)

   if need_comments:
      ctx.comments = _tokens_mod.scan_comments(source_text)

   return ctx


def filter_rules(rules_list, args) -> list:

   selected = list(rules_list)

   wanted = _split_rule_args(args.rule)
   banned = _split_rule_args(args.exclude_rule)

   if wanted:
      selected = [r for r in selected if r.id in wanted]

   if banned:
      selected = [r for r in selected if r.id not in banned]

   if args.severity == 'hard':
      selected = [r for r in selected if r.severity == 'error']
   elif args.severity == 'soft':
      selected = [r for r in selected if r.severity == 'warning']

   return selected


def _split_rule_args(raw_list) -> set:
   """Each element of `raw_list` can be a single rule ID or a
   comma-separated list. Flatten to a set."""

   out = set()

   for entry in raw_list:

      for piece in entry.split(','):

         piece = piece.strip()

         if piece:
            out.add(piece)

   return out


def _status_for(diags) -> str:
   """Pick the worst severity present in `diags`. Used to label each
   file's progress line: `OK` (nothing fired), `WARN` (only
   warnings), `FAIL` (at least one error)."""

   if not diags:
      return 'OK'

   if any(d.severity == 'error' for d in diags):
      return 'FAIL'

   return 'WARN'


# Padded so all labels align in a column of width 4.
_STATUS_LABEL = {
   'OK':   ' OK ',
   'WARN': 'WARN',
   'FAIL': 'FAIL',
   'SKIP': 'SKIP',
}


def _emit_status(file_display: str, status: str, color_mode: str,
                 stream) -> None:
   """Append `... [STATUS]` after a previously-printed `Checking
   <file> ` line. Color the bracketed label only -- everything else
   stays plain so terminals without color rendering still get the
   text."""

   use_color = reporter._decide_color(color_mode, stream)  # pylint: disable=protected-access

   code = {
      'OK':   reporter._GREEN,
      'WARN': reporter._YELLOW,
      'FAIL': reporter._RED,
      'SKIP': reporter._DIM + reporter._CYAN,
   }[status]

   label = '[{}]'.format(_STATUS_LABEL[status])

   if use_color:
      label = reporter._BOLD + code + label + reporter._RESET

   stream.write('... {}\n'.format(label))
   stream.flush()


def _display_path(p: Path, repo_root) -> str:
   """Show path relative to the repo root when possible -- shorter
   and clearer in --all sweeps; absolute path for files outside
   the tree."""

   if repo_root is None:
      return str(p)

   try:
      return str(p.relative_to(repo_root))
   except ValueError:
      return str(p)


def cmd_check(args) -> int:

   repo_root = _parser_mod.find_repo_root()
   build_dir = _parser_mod.resolve_build_dir(args.build_dir)

   files = collect_files(args, repo_root)

   # v1 is C-only: drop .cpp/.hpp inputs silently.
   files = [f for f in files if f.suffix in ('.c', '.h')]

   if not files:
      sys.stderr.write(
         "error: no .c or .h files to check (v1 is C-only)\n"
      )
      return 1

   rules = filter_rules(ALL_RULES, args)

   if not rules:
      sys.stderr.write("error: no rules selected\n")
      return 1

   need_tu_any = any(r.needs_tu for r in rules)
   parser_obj = _parser_mod.Parser(build_dir) if need_tu_any else None

   fmt = 'jsonl' if args.json else args.format

   # Per-file progress display: text format + (--all or multi-file
   # input). JSONL stays a clean stream of records.
   show_progress = (fmt == 'text') and (args.all or len(files) > 1)

   all_diags = []

   for f in files:

      if not f.exists():
         sys.stderr.write(
            "warning: skipping missing file {}\n".format(f)
         )
         continue

      # Resolve `.style.yml` cascade for this file. The config can
      # disable rules, restrict to a whitelist, or mark the file
      # entirely ignored.
      file_cfg = _config.resolve_config(f, repo_root)

      if file_cfg.ignore:

         if show_progress:
            sys.stdout.write('Checking {} '.format(
               _display_path(f, repo_root)
            ))
            sys.stdout.flush()
            _emit_status(
               _display_path(f, repo_root),
               'SKIP',
               args.color,
               sys.stdout,
            )

         continue

      applicable = [r for r in rules if r.applies_to_file(f)]
      applicable = _config.apply_to_rules(applicable, file_cfg)

      if not applicable:
         continue

      need_tu = any(r.needs_tu for r in applicable)
      need_cm = any(r.needs_comments for r in applicable)

      if show_progress:
         sys.stdout.write('Checking {} '.format(
            _display_path(f, repo_root)
         ))
         sys.stdout.flush()

      ctx = build_context(f, parser_obj, need_tu, need_cm)

      file_diags = []

      for r in applicable:

         try:
            diags = r.check(ctx)
         except Exception as e:
            sys.stderr.write(
               "error: rule '{}' raised on {}: {}\n".format(r.id, f, e)
            )
            continue

         # Backfill prettiness score from the rule's default if a
         # rule didn't set it explicitly per-diagnostic.
         for d in diags:
            if d.score == 0.0:
               d.score = r.default_score

         file_diags.extend(diags)

      # Build the per-function summary. The aggregator needs the TU
      # to find function extents; if a rule didn't request a TU the
      # context won't have one, so the summary will be file-level only.
      file_summary = _agg.build_file_summary(
         file_path=f,
         tu=ctx.tu,
         lines=ctx.lines,
         diagnostics=file_diags,
      )

      if show_progress:
         _emit_status(
            _display_path(f, repo_root),
            _status_for(file_diags),
            args.color,
            sys.stdout,
         )

         if file_diags:

            reporter.emit(
               file_diags,
               fmt=fmt,
               color_mode=args.color,
               with_total=False,
            )

            if args.summary and file_summary.functions:
               reporter.emit_function_summaries(
                  file_summary,
                  color_mode=args.color,
                  stream=sys.stdout,
               )

            sys.stdout.write('\n')

      elif file_diags and not args.json and args.summary \
            and file_summary.functions:

         # Single-file text mode: print summary at the end too if
         # there's something to show.
         reporter.emit(
            file_diags,
            fmt=fmt,
            color_mode=args.color,
            with_total=True,
         )
         reporter.emit_function_summaries(
            file_summary,
            color_mode=args.color,
            stream=sys.stdout,
         )
         all_diags.extend(file_diags)
         continue

      all_diags.extend(file_diags)

   if show_progress:

      # Grand summary across all files.
      total = sum(d.score for d in all_diags)

      if total != 0.0:

         use_color = reporter._decide_color(  # pylint: disable=protected-access
            args.color, sys.stdout
         )
         line = ('total prettiness: {:+.1f} across {} '
                 'diagnostic(s) in {} file(s)\n').format(
            total, len(all_diags), len(files)
         )

         if use_color:
            line = reporter._BOLD + line + reporter._RESET

         sys.stdout.write(line)

   elif fmt == 'jsonl':

      # JSONL: no per-file emission happened above; flush the whole
      # collected stream now.
      reporter.emit(all_diags, fmt=fmt, color_mode=args.color)

   # Single-file text mode: per-file emit already happened in the
   # elif branch above (or no diagnostics, no emit). Nothing left
   # to print here.

   return 0


def _severity_tag(r) -> str:
   """Short label used in `list-rules`: HARD for error-severity
   rules (defects), SOFT for warning-severity prettiness rules."""

   return 'HARD' if r.severity == 'error' else 'SOFT'


def _tier_of(r) -> str:

   if r.severity == 'error':
      return 'HARD'

   s = r.default_score

   if s <= -3.0:
      return 'STRONG'

   if s <= -1.0:
      return 'MEDIUM'

   if s <= -0.4:
      return 'SOFT'

   return 'NUDGE'


_TIER_ORDER = ['HARD', 'STRONG', 'MEDIUM', 'SOFT', 'NUDGE']
_TIER_LABEL = {
   'HARD':   'HARD   (defects)',
   'STRONG': 'STRONG (-3.0, strong prefs)',
   'MEDIUM': 'MEDIUM (-1.5, conventions)',
   'SOFT':   'SOFT   (-0.5, mild prefs)',
   'NUDGE':  'NUDGE  (-0.2, low-threshold)',
}


def cmd_list_rules(args) -> int:  # pylint: disable=unused-argument

   buckets = {t: [] for t in _TIER_ORDER}

   for r in ALL_RULES:
      buckets[_tier_of(r)].append(r)

   for t in _TIER_ORDER:

      if not buckets[t]:
         continue

      sys.stdout.write('\n=== {} -- {} rule(s) ===\n'.format(
         _TIER_LABEL[t], len(buckets[t])
      ))

      for r in buckets[t]:
         desc = r.description.split('\n')[0]

         if len(desc) > 70:
            desc = desc[:67] + '...'

         sys.stdout.write(
            "  {:45} [{}] (score {:+.1f}) {}\n".format(
               r.id, r.layers, r.default_score, desc
            )
         )

   sys.stdout.write('\n')
   return 0


def cmd_explain(args) -> int:

   r = RULES_BY_ID.get(args.rule_id)

   if r is None:
      sys.stderr.write("error: no such rule: {}\n".format(args.rule_id))
      return 1

   sys.stdout.write("rule:      {}\n".format(r.id))
   sys.stdout.write("class:     {} ({} severity)\n".format(
      _severity_tag(r), r.severity
   ))
   sys.stdout.write(
      "score:     {:+.1f} (default per-diagnostic "
      "prettiness penalty)\n".format(r.default_score)
   )
   sys.stdout.write("layers:    {}\n".format(r.layers))
   sys.stdout.write("\n")
   sys.stdout.write(r.explain())
   sys.stdout.write("\n")

   return 0


# Directory containing the `_style_check` package -- which is also
# `scripts/dev/claude/`. We anchor coverage HTML output here.
_PACKAGE_PARENT = Path(__file__).resolve().parent.parent

# Public test module the `test` sub-command runs.
_TEST_MODULE = '_style_check.tests.test_rules'

# Coverage HTML output goes to `scripts/dev/claude/htmlcov/`.
_HTMLCOV_DIR = _PACKAGE_PARENT / 'htmlcov'


def _run_tests_plain(verbose: bool, failfast: bool) -> int:

   cmd = [sys.executable, '-m', 'unittest']

   if verbose:
      cmd.append('-v')

   if failfast:
      cmd.append('-f')

   cmd.append(_TEST_MODULE)

   env = os.environ.copy()
   env['PYTHONPATH'] = str(_PACKAGE_PARENT)

   return subprocess.run(cmd, cwd=str(_PACKAGE_PARENT), env=env,
                         check=False).returncode


def _run_tests_with_coverage(verbose: bool, failfast: bool) -> int:

   # Require coverage to be importable -- give a clear hint if missing.
   try:
      import coverage as _cov  # noqa: F401  pylint: disable=import-outside-toplevel,unused-import
   except ImportError:
      sys.stderr.write(
         "error: --coverage requires the `coverage` package.\n"
         "Install with:  pip3 install --user coverage\n"
         "(on Debian/PEP-668 systems add --break-system-packages)\n"
      )
      return 2

   env = os.environ.copy()
   env['PYTHONPATH'] = str(_PACKAGE_PARENT)

   # 1) Run tests under coverage. Scope to `_style_check.rules` --
   # that is the meaningful code that synthetic fixtures exercise.
   # The rest of the package (CLI dispatcher, parser wrapper, raw-
   # text helpers) is covered implicitly by every rule invocation
   # that uses it, but the headline number tracks rule coverage.
   run_cmd = [
      sys.executable, '-m', 'coverage', 'run',
      '--source=_style_check.rules',
      '-m', 'unittest'
   ]

   if verbose:
      run_cmd.append('-v')

   if failfast:
      run_cmd.append('-f')

   run_cmd.append(_TEST_MODULE)

   rc = subprocess.run(run_cmd, cwd=str(_PACKAGE_PARENT), env=env,
                       check=False).returncode

   if rc != 0:
      sys.stderr.write(
         "error: tests failed -- HTML coverage report not generated\n"
      )
      return rc

   # 2) Emit text summary to stdout.
   subprocess.run(
      [sys.executable, '-m', 'coverage', 'report'],
      cwd=str(_PACKAGE_PARENT), env=env, check=False
   )

   # 3) Generate HTML report into scripts/dev/claude/htmlcov/.
   subprocess.run(
      [sys.executable, '-m', 'coverage', 'html',
       '-d', str(_HTMLCOV_DIR)],
      cwd=str(_PACKAGE_PARENT), env=env, check=False
   )

   sys.stdout.write(
      "\nHTML coverage report: {}/index.html\n".format(_HTMLCOV_DIR)
   )
   return 0


def cmd_test(args) -> int:

   if args.coverage:
      return _run_tests_with_coverage(args.verbose, args.failfast)

   return _run_tests_plain(args.verbose, args.failfast)


_DESCRIPTION = (
   'Tilck C coding-style checker (libclang + raw text).\n\n'
   'Reports formatting violations as machine-readable JSONL or\n'
   'human-readable text. Rules are derived from CLAUDE.md +\n'
   'docs/contributing.md + the empirical kernel/userspace corpus.\n\n'
   'Sub-commands:\n'
   '  check       Check files for style violations\n'
   '  list-rules  List every registered rule, one per line\n'
   '  explain     Print the full description of one rule\n'
   '  test        Run the unit test suite (optional --coverage)\n'
)

_EPILOG = (
   'Examples:\n'
   '  # Human-readable check of two files\n'
   '  style_check check kernel/poll.c kernel/sched.c\n\n'
   '  # JSONL output (one diag per line) for tooling consumption\n'
   '  style_check check --json kernel/poll.c\n\n'
   '  # Files changed since master\n'
   '  style_check check --since master\n\n'
   '  # Sweep every .c/.h file tracked by git (defects in the source,\n'
   '  # not in the tool)\n'
   '  style_check check --all\n\n'
   '  # Run a single rule across selected files\n'
   '  style_check check --rule sizeof_parens kernel/*.c\n\n'
   '  # Skip a noisy rule\n'
   '  style_check check --exclude-rule blank_line_after_decl_block ...\n\n'
   '  # List rules / look up one rule\n'
   '  style_check list-rules\n'
   '  style_check explain include_order\n\n'
   '  # Run unit tests; --coverage also writes HTML to\n'
   '  # scripts/dev/claude/htmlcov/index.html\n'
   '  style_check test\n'
   '  style_check test --coverage\n'
)


def build_argparser():

   ap = argparse.ArgumentParser(
      prog='style_check',
      description=_DESCRIPTION,
      epilog=_EPILOG,
      formatter_class=argparse.RawDescriptionHelpFormatter,
   )

   sub = ap.add_subparsers(dest='cmd', required=True, metavar='COMMAND')

   check_p = sub.add_parser('check', help='Check files for violations')

   check_p.add_argument('files', nargs='*', help='Files to check')

   check_p.add_argument(
      '--since',
      help='Also check files changed since this git ref'
   )

   check_p.add_argument(
      '--all',
      action='store_true',
      help=('Also check every .c / .h file tracked by git in the '
            'current repository (corpus sweep -- failures are '
            'defects in the source code, not the tool)')
   )

   check_p.add_argument(
      '--format',
      choices=['jsonl', 'text'],
      default='text',
      help='Output format (default: text -- human-friendly)'
   )

   check_p.add_argument(
      '--json',
      action='store_true',
      help='Shortcut for --format jsonl (machine-readable output)'
   )

   check_p.add_argument(
      '--color',
      choices=['auto', 'always', 'never'],
      default='auto',
      help=('Colorize text output. `auto` (default) enables colors '
            'when stdout is a TTY and NO_COLOR is unset; `always` '
            'forces them on (e.g. when piping into a pager that '
            'understands ANSI); `never` disables them')
   )

   check_p.add_argument(
      '-r', '--rule',
      action='append',
      default=[],
      metavar='RULES',
      help=('Only run this rule. Accepts a single rule ID or a '
            'comma-separated list; repeatable. Examples: '
            '`-r cols_80`, `-r cols_80,pragma_once`, '
            '`-r cols_80 -r pragma_once`.')
   )

   check_p.add_argument(
      '-x', '--exclude-rule',
      action='append',
      default=[],
      metavar='RULES',
      help=('Skip this rule. Accepts a single rule ID or a '
            'comma-separated list; repeatable.')
   )

   check_p.add_argument(
      '--severity',
      choices=['hard', 'soft', 'all'],
      default='all',
      help=('Filter by rule class. `hard` shows error-severity '
            'defects only; `soft` shows warning-severity prettiness '
            'preferences only; `all` (default) shows both')
   )

   check_p.add_argument(
      '--summary',
      action=argparse.BooleanOptionalAction,
      default=True,
      help=('Show per-function prettiness summary (total / '
            'normalized score / verdict) under each file with '
            'diagnostics. Default: on. Use --no-summary to disable.')
   )

   check_p.add_argument(
      '--build-dir',
      default='build/compile_db',
      help='Path to the merged compile_commands.json directory'
   )

   check_p.set_defaults(func=cmd_check)

   list_p = sub.add_parser('list-rules', help='List all rules')
   list_p.set_defaults(func=cmd_list_rules)

   explain_p = sub.add_parser('explain', help='Explain a rule')
   explain_p.add_argument('rule_id', help='Rule ID')
   explain_p.set_defaults(func=cmd_explain)

   test_p = sub.add_parser(
      'test',
      help='Run the unit test suite (optional --coverage)',
      description=(
         'Run the unit tests under _style_check/tests/. With '
         '--coverage, also collect line coverage via coverage.py '
         'and write an HTML report to scripts/dev/claude/htmlcov/.'
      ),
   )

   test_p.add_argument(
      '--coverage',
      action='store_true',
      help='Collect coverage and write HTML to scripts/dev/claude/htmlcov/'
   )

   test_p.add_argument(
      '-v', '--verbose',
      action='store_true',
      help='Verbose unittest output (one line per test)'
   )

   test_p.add_argument(
      '-f', '--failfast',
      action='store_true',
      help='Stop on first failing test'
   )

   test_p.set_defaults(func=cmd_test)

   return ap


def main():

   ap = build_argparser()
   args = ap.parse_args()
   return args.func(args)


###############################
if __name__ == '__main__':
   sys.exit(main() or 0)
