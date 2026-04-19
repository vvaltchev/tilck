#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Unit tests for gen_kconfig.py."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

import gen_kconfig  # noqa: E402


def _write_sidecar(path: Path, records: list[dict]) -> None:
    path.write_text("\n".join(json.dumps(r) for r in records) + "\n")


def _rec(**overrides) -> dict:
    """Build a record with sensible defaults, overridable via kwargs."""
    base = {
        "name": "FOO",
        "type": "bool",
        "category": "Kernel/Demo",
        "default": "ON",
        "current": "ON",
        "depends": [],
        "help": "Demo option",
    }
    base.update(overrides)
    return base


class TestLoadSidecar(unittest.TestCase):

    def test_empty_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "empty.json"
            p.write_text("")
            self.assertEqual(gen_kconfig.load_sidecar(p), [])

    def test_missing_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(
                gen_kconfig.load_sidecar(Path(tmp) / "nope.json"), []
            )

    def test_skips_blank_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "s.json"
            p.write_text("\n" + json.dumps(_rec()) + "\n\n")
            self.assertEqual(len(gen_kconfig.load_sidecar(p)), 1)

    def test_invalid_json_raises(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "bad.json"
            p.write_text("{not valid\n")
            with self.assertRaises(ValueError):
                gen_kconfig.load_sidecar(p)


class TestValidate(unittest.TestCase):

    def test_empty_records_is_valid(self):
        self.assertEqual(gen_kconfig.validate([]), [])

    def test_missing_fields_flagged(self):
        errs = gen_kconfig.validate([{"name": "FOO", "type": "bool"}])
        self.assertTrue(any("missing fields" in e for e in errs))

    def test_unknown_type_flagged(self):
        errs = gen_kconfig.validate([_rec(type="bogus")])
        self.assertTrue(any("unknown type" in e for e in errs))

    def test_duplicate_name_flagged(self):
        errs = gen_kconfig.validate([_rec(), _rec()])
        self.assertTrue(any("duplicate name" in e for e in errs))

    def test_enum_requires_strings(self):
        errs = gen_kconfig.validate([_rec(type="enum", default="a")])
        self.assertTrue(any("non-empty 'strings'" in e for e in errs))

    def test_enum_default_must_be_in_strings(self):
        errs = gen_kconfig.validate([
            _rec(type="enum", strings=["a", "b"], default="c")
        ])
        self.assertTrue(
            any("not in strings" in e for e in errs),
            msg=errs,
        )

    def test_enum_default_in_strings_passes(self):
        self.assertEqual(
            gen_kconfig.validate([
                _rec(type="enum", strings=["a", "b"], default="a", current="a")
            ]),
            [],
        )

    def test_int_default_validation(self):
        self.assertEqual(
            gen_kconfig.validate([_rec(type="int", default="-42", current="-42")]),
            [],
        )
        self.assertTrue(gen_kconfig.validate([
            _rec(type="int", default="0xff", current="0xff")
        ]))

    def test_uint_rejects_negative(self):
        self.assertTrue(gen_kconfig.validate([
            _rec(type="uint", default="-1", current="-1")
        ]))

    def test_uint_accepts_zero_and_positive(self):
        self.assertEqual(
            gen_kconfig.validate([_rec(type="uint", default="0", current="0")]),
            [],
        )
        self.assertEqual(
            gen_kconfig.validate([
                _rec(type="uint", default="12345", current="12345")
            ]),
            [],
        )

    def test_addr_requires_0x_prefix(self):
        self.assertTrue(gen_kconfig.validate([
            _rec(type="addr", default="ff80", current="ff80")
        ]))

    def test_addr_accepts_hex(self):
        self.assertEqual(
            gen_kconfig.validate([_rec(
                type="addr", default="0xFfFf80000000", current="0xFfFf80000000"
            )]),
            [],
        )

    def test_depends_unknown_name_flagged(self):
        errs = gen_kconfig.validate([
            _rec(depends=["MISSING"]),
        ])
        self.assertTrue(any("unknown option" in e for e in errs))

    def test_depends_negation_resolves(self):
        errs = gen_kconfig.validate([
            _rec(name="A", depends=["!B"]),
            _rec(name="B"),
        ])
        self.assertEqual(errs, [])

    def test_depends_complex_expression_passthrough(self):
        # Complex boolean expressions aren't validated (passed through
        # to Kconfig). No spurious "unknown option" errors.
        errs = gen_kconfig.validate([
            _rec(name="A", depends=["B && !C"]),
            _rec(name="B"),
            _rec(name="C"),
        ])
        self.assertEqual(errs, [])


class TestGenerate(unittest.TestCase):

    def _gen(self, records):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        gen_kconfig.generate(records, Path(tmp.name))
        return Path(tmp.name)

    def test_bool_basics(self):
        d = self._gen([_rec(name="X", category="K", type="bool",
                            default="ON", current="OFF")])
        kcfg = (d / "Kconfig.k").read_text()
        self.assertIn("config X", kcfg)
        self.assertIn('bool "Demo option"', kcfg)
        self.assertIn("default y", kcfg)
        # .config reflects current, not default
        self.assertIn("# CONFIG_X is not set", (d / ".config").read_text())

    def test_bool_truthy_current_y(self):
        d = self._gen([_rec(name="X", type="bool", current="ON")])
        self.assertIn("CONFIG_X=y", (d / ".config").read_text())

    def test_int_and_uint_and_addr(self):
        d = self._gen([
            _rec(name="I", category="K", type="int",
                 default="-5", current="-5"),
            _rec(name="U", category="K", type="uint",
                 default="100", current="100"),
            _rec(name="A", category="K", type="addr",
                 default="0xdeadbeef", current="0xdeadbeef"),
        ])
        kcfg = (d / "Kconfig.k").read_text()
        self.assertIn("int \"Demo option\"", kcfg)
        self.assertIn("range 0 2147483647", kcfg)
        self.assertIn("hex \"Demo option\"", kcfg)
        cfg = (d / ".config").read_text()
        self.assertIn("CONFIG_I=-5", cfg)
        self.assertIn("CONFIG_U=100", cfg)
        self.assertIn("CONFIG_A=0xdeadbeef", cfg)

    def test_enum_emits_string_with_help_annotation(self):
        d = self._gen([_rec(
            name="MODE", type="enum",
            strings=["auto", "fast", "slow"],
            default="auto", current="fast",
        )])
        kcfg = (d / "Kconfig.kernel").read_text()
        self.assertIn('string "Demo option"', kcfg)
        self.assertIn("Valid values: auto, fast, slow", kcfg)
        cfg = (d / ".config").read_text()
        self.assertIn('CONFIG_MODE="fast"', cfg)

    def test_depends_emitted(self):
        d = self._gen([
            _rec(name="A", category="K", depends=["B", "!C"]),
            _rec(name="B", category="K"),
            _rec(name="C", category="K"),
        ])
        kcfg = (d / "Kconfig.k").read_text()
        self.assertIn("depends on B", kcfg)
        self.assertIn("depends on !C", kcfg)

    def test_multiple_top_categories_produce_separate_files(self):
        d = self._gen([
            _rec(name="K1", category="Kernel/A"),
            _rec(name="M1", category="Modules/B"),
        ])
        self.assertTrue((d / "Kconfig.kernel").exists())
        self.assertTrue((d / "Kconfig.modules").exists())
        root = (d / "Kconfig").read_text()
        self.assertIn('source "Kconfig.kernel"', root)
        self.assertIn('source "Kconfig.modules"', root)

    def test_nested_subcategories_become_nested_menus(self):
        d = self._gen([
            _rec(name="X", category="Kernel/Memory/Heap"),
            _rec(name="Y", category="Kernel/Memory"),
            _rec(name="Z", category="Kernel/Timer"),
        ])
        kcfg = (d / "Kconfig.kernel").read_text()
        # Expect a "Kernel" top menu, with sub-menus for Memory (with
        # sub-sub Heap) and Timer.
        self.assertIn('menu "Kernel"', kcfg)
        self.assertIn('menu "Memory"', kcfg)
        self.assertIn('menu "Heap"', kcfg)
        self.assertIn('menu "Timer"', kcfg)

    def test_empty_records_produces_only_mainmenu(self):
        d = self._gen([])
        root = (d / "Kconfig").read_text()
        self.assertIn('mainmenu "Tilck Configuration"', root)

    def test_help_multiline(self):
        d = self._gen([_rec(
            name="H", category="K",
            help="Line one\nLine two\nLine three",
        )])
        kcfg = (d / "Kconfig.k").read_text()
        # Summary (first line) used as prompt.
        self.assertIn('"Line one"', kcfg)
        # All lines present in help block.
        self.assertIn("Line one", kcfg)
        self.assertIn("Line two", kcfg)
        self.assertIn("Line three", kcfg)


if __name__ == "__main__":
    unittest.main()
