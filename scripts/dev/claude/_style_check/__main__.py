#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import, bad-indentation, wildcard-import
# pylint: disable=missing-function-docstring, missing-class-docstring
# pylint: disable=invalid-name, broad-except, consider-using-f-string

import sys
import argparse
import subprocess

from pathlib import Path

from . import parser as _parser_mod
from . import reporter
from . import extents
from . import tokens as _tokens_mod
from .rules import ALL_RULES, RULES_BY_ID
from .rules.base import CheckContext


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

   if args.rule:
      wanted = set(args.rule)
      selected = [r for r in selected if r.id in wanted]

   if args.exclude_rule:
      banned = set(args.exclude_rule)
      selected = [r for r in selected if r.id not in banned]

   return selected


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

   all_diags = []

   for f in files:

      if not f.exists():
         sys.stderr.write(
            "warning: skipping missing file {}\n".format(f)
         )
         continue

      applicable = [r for r in rules if r.applies_to_file(f)]

      if not applicable:
         continue

      need_tu = any(r.needs_tu for r in applicable)
      need_cm = any(r.needs_comments for r in applicable)

      ctx = build_context(f, parser_obj, need_tu, need_cm)

      for r in applicable:

         try:
            diags = r.check(ctx)
         except Exception as e:
            sys.stderr.write(
               "error: rule '{}' raised on {}: {}\n".format(r.id, f, e)
            )
            continue

         all_diags.extend(diags)

   reporter.emit(all_diags, fmt=args.format)
   return 0


def cmd_list_rules(args) -> int:  # pylint: disable=unused-argument

   for r in ALL_RULES:
      sys.stdout.write(
         "{:50} [{}] {}\n".format(r.id, r.layers, r.description)
      )

   return 0


def cmd_explain(args) -> int:

   r = RULES_BY_ID.get(args.rule_id)

   if r is None:
      sys.stderr.write("error: no such rule: {}\n".format(args.rule_id))
      return 1

   sys.stdout.write("rule:      {}\n".format(r.id))
   sys.stdout.write("layers:    {}\n".format(r.layers))
   sys.stdout.write("severity:  {}\n".format(r.severity))
   sys.stdout.write("\n")
   sys.stdout.write(r.explain())
   sys.stdout.write("\n")

   return 0


def build_argparser():

   ap = argparse.ArgumentParser(
      prog='style_check',
      description='Tilck C coding-style checker (libclang + raw text).'
   )

   sub = ap.add_subparsers(dest='cmd', required=True)

   check_p = sub.add_parser('check', help='Check files for violations')

   check_p.add_argument('files', nargs='*', help='Files to check')

   check_p.add_argument(
      '--since',
      help='Also check files changed since this git ref'
   )

   check_p.add_argument(
      '--format',
      choices=['jsonl', 'text'],
      default='jsonl',
      help='Output format (default: jsonl)'
   )

   check_p.add_argument(
      '--rule',
      action='append',
      default=[],
      help='Only run this rule (repeatable)'
   )

   check_p.add_argument(
      '--exclude-rule',
      action='append',
      default=[],
      help='Skip this rule (repeatable)'
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

   return ap


def main():

   ap = build_argparser()
   args = ap.parse_args()
   return args.func(args)


###############################
if __name__ == '__main__':
   sys.exit(main() or 0)
