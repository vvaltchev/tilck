# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=bad-indentation, missing-module-docstring
# pylint: disable=missing-class-docstring, missing-function-docstring
# pylint: disable=broad-except, consider-using-f-string

import os
import re as _re

from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional, Set, Dict, Tuple


CONFIG_FILE_NAME = '.style.yml'


@dataclass
class StyleConfig:
   """Resolved per-file configuration: the result of merging all
   `.style.yml` files from the repo root down to the file's parent
   directory."""

   # If True, the file is excluded from all checks entirely.
   ignore: bool = False

   # Rules disabled for this file. Set is the UNION of `disabled`
   # lists from every config in the path.
   disabled: Set[str] = field(default_factory=set)

   # Whitelist of rule IDs. None means "all rules". When set,
   # ONLY these rules run. Deeper configs replace shallower ones
   # (last `enabled_only` setting on the path wins).
   enabled_only: Optional[Set[str]] = None

   # Provenance: list of (config_path, why) for debug / introspection.
   sources: List[Tuple[Path, str]] = field(default_factory=list)


def _parse_simple_yaml(text: str) -> Dict[str, object]:
   """A tiny YAML subset for our config schema. Supports:

       key: value         (bool / int / quoted-or-bare string)
       key:               (followed by indented `- item` lines)
         - item
         - item
       # comments and blank lines

   Returns a dict. Unknown / malformed input is silently skipped --
   the caller is responsible for validating values."""

   result: Dict[str, object] = {}
   current_list: Optional[List[str]] = None

   for raw_line in text.split('\n'):

      # Strip line-trailing comments unless the `#` is inside a
      # quoted string (we don't support quoted strings with `#`
      # in this minimal parser; raw `#` always starts a comment).
      hash_pos = raw_line.find('#')

      if hash_pos != -1:
         line = raw_line[:hash_pos]
      else:
         line = raw_line

      if not line.strip():
         continue

      stripped = line.lstrip()

      # List item under the most recently opened key.
      if stripped.startswith('- '):

         if current_list is None:
            continue   # orphan list item, ignore

         item = stripped[2:].strip()

         # Strip optional surrounding quotes
         if (len(item) >= 2
               and item[0] == item[-1]
               and item[0] in ('"', "'")):
            item = item[1:-1]

         if item:
            current_list.append(item)

         continue

      # key: value
      if ':' not in line:
         continue

      key, _, value = line.partition(':')
      key = key.strip()
      value = value.strip()

      if not key:
         continue

      if not value:
         # Open a list for this key.
         current_list = []
         result[key] = current_list
         continue

      current_list = None

      # Scalar value
      lower = value.lower()

      if lower in ('true', 'yes'):
         result[key] = True
      elif lower in ('false', 'no'):
         result[key] = False

      elif (len(value) >= 2
            and value[0] == value[-1]
            and value[0] in ('"', "'")):
         result[key] = value[1:-1]
      else:

         try:
            result[key] = int(value)
         except ValueError:
            result[key] = value

   return result


def load_config_file(path: Path) -> Dict[str, object]:
   """Read and parse a `.style.yml` file. Returns an empty dict on
   any I/O or parse failure (configs are best-effort)."""

   try:
      text = path.read_text(encoding='utf-8', errors='replace')
   except (OSError, IOError):
      return {}

   return _parse_simple_yaml(text)


def _find_configs_along_path(file_path: Path,
                              repo_root: Optional[Path]) -> List[Path]:
   """Return every `.style.yml` from repo_root down to the file's
   parent directory, ROOT-FIRST. The orchestrator applies them in
   that order so deeper configs override shallower ones."""

   anchor = repo_root if repo_root is not None else Path('/')

   try:
      anchor_resolved = anchor.resolve()
   except Exception:
      anchor_resolved = anchor

   try:
      cur = file_path.resolve().parent
   except Exception:
      cur = file_path.parent

   # Collect from leaf up to root, then reverse.
   collected: List[Path] = []

   while True:

      candidate = cur / CONFIG_FILE_NAME

      if candidate.is_file():
         collected.append(candidate)

      # Stop at the anchor or filesystem root.
      if cur == anchor_resolved:
         break

      parent = cur.parent

      if parent == cur:
         break   # at filesystem root

      cur = parent

   collected.reverse()
   return collected


_INLINE_DISABLE_RE = _re.compile(
   r'/\*\s*style_check:\s*disable\s+([\w,\s]+)\s*\*/'
)

_INLINE_SCAN_LINES = 15


def _scan_inline_disables(file_path: Path) -> Set[str]:
   """Scan the first few lines of a file for inline disable markers.

   Recognised form: /* style_check: disable rule1, rule2 */
   Returns the set of rule IDs to suppress for this file."""

   disabled = set()  # type: Set[str]

   try:
      with open(file_path, encoding='utf-8', errors='replace') as fh:
         for n, line in enumerate(fh):
            if n >= _INLINE_SCAN_LINES:
               break
            m = _INLINE_DISABLE_RE.search(line)
            if m:
               for tok in m.group(1).split(','):
                  tok = tok.strip()
                  if tok:
                     disabled.add(tok)
   except (OSError, IOError):
      pass

   return disabled


def resolve_config(file_path: Path,
                   repo_root: Optional[Path]) -> StyleConfig:
   """Walk the directory chain from `repo_root` down to
   `file_path`'s parent, applying each `.style.yml` in order to
   produce the final `StyleConfig` for this file.

   Also scans the file's first few lines for inline disable markers
   of the form: /* style_check: disable rule_id */"""

   cfg = StyleConfig()

   # Inline file-level disables.
   inline = _scan_inline_disables(file_path)

   if inline:
      cfg.disabled |= inline

   for cfg_path in _find_configs_along_path(file_path, repo_root):

      raw = load_config_file(cfg_path)

      if not raw:
         continue

      changed = []

      ig = raw.get('ignore')

      if isinstance(ig, bool):
         cfg.ignore = cfg.ignore or ig

         if ig:
            changed.append('ignore=true')

      dis = raw.get('disabled')

      if isinstance(dis, list):
         new_items = {str(x) for x in dis if x}
         cfg.disabled |= new_items

         if new_items:
            changed.append('disabled+={}'.format(sorted(new_items)))

      en = raw.get('enabled_only')

      if isinstance(en, list):
         cfg.enabled_only = {str(x) for x in en if x}
         changed.append(
            'enabled_only={}'.format(sorted(cfg.enabled_only))
         )

      if changed:
         cfg.sources.append((cfg_path, '; '.join(changed)))

   return cfg


def scan_block_disables(lines: List[str]) -> List[Tuple[int, int, Set[str]]]:
   """Scan for block-scoped style_check annotations inside { } blocks.

   A `/* style_check: disable rule1, rule2 */` comment that appears
   inside a function body (after `{`) suppresses the listed rules for
   the enclosing block only — from the annotation line to the matching
   `}`.

   Returns a list of (start_line, end_line, disabled_rules) tuples
   (1-based, inclusive)."""

   ranges = []

   for i, line in enumerate(lines):

      m = _INLINE_DISABLE_RE.search(line)

      if not m:
         continue

      ln = i + 1   # 1-based

      # Is this inside a block (not at file scope)?
      # File-level markers are in the first _INLINE_SCAN_LINES lines
      # and are already handled by _scan_inline_disables. Skip those.
      if i < _INLINE_SCAN_LINES:
         continue

      rules = set()

      for tok in m.group(1).split(','):
         tok = tok.strip()
         if tok:
            rules.add(tok)

      if not rules:
         continue

      # Find the end of the enclosing block: walk forward counting
      # braces. We start at depth 0 (the annotation line) and look
      # for the line where depth drops below 0 (matching `}`).
      depth = 0
      end = len(lines)

      for j in range(i + 1, len(lines)):
         for ch in lines[j]:
            if ch == '{':
               depth += 1
            elif ch == '}':
               depth -= 1
               if depth < 0:
                  end = j + 1  # 1-based
                  break

         if depth < 0:
            break

      ranges.append((ln, end, rules))

   return ranges


def filter_block_disables(diagnostics, block_ranges):
   """Remove diagnostics suppressed by block-scoped annotations."""

   if not block_ranges:
      return diagnostics

   out = []

   for d in diagnostics:

      suppressed = False

      for start, end, rules in block_ranges:
         if start <= d.line <= end and d.rule in rules:
            suppressed = True
            break

      if not suppressed:
         out.append(d)

   return out


def apply_to_rules(rules_list, cfg: StyleConfig) -> list:
   """Filter a list of Rule instances by the resolved config.
   Doesn't handle `ignore` -- the caller should short-circuit
   when `cfg.ignore` is True."""

   selected = list(rules_list)

   if cfg.enabled_only is not None:
      selected = [r for r in selected if r.id in cfg.enabled_only]

   if cfg.disabled:
      selected = [r for r in selected if r.id not in cfg.disabled]

   return selected
