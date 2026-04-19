#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Unit tests for apply_config.py."""

from __future__ import annotations

import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

import apply_config  # noqa: E402


def _tmp_file(contents: str) -> Path:
    f = tempfile.NamedTemporaryFile(
        mode="w", delete=False, suffix=".txt"
    )
    f.write(contents)
    f.close()
    p = Path(f.name)
    unittest.TestCase.addClassCleanup(unittest.TestCase, p.unlink)
    return p


class TestParseDotconfig(unittest.TestCase):

    def _parse(self, text: str) -> dict[str, str]:
        with tempfile.NamedTemporaryFile(
            mode="w", delete=False, suffix=".config"
        ) as f:
            f.write(text)
            path = Path(f.name)
        try:
            return apply_config.parse_dotconfig(path)
        finally:
            path.unlink()

    def test_bool_yes(self):
        self.assertEqual(self._parse("CONFIG_FOO=y\n"), {"FOO": "y"})

    def test_bool_not_set(self):
        self.assertEqual(
            self._parse("# CONFIG_FOO is not set\n"), {"FOO": "n"}
        )

    def test_int(self):
        self.assertEqual(
            self._parse("CONFIG_HZ=250\n"), {"HZ": "250"}
        )

    def test_hex(self):
        self.assertEqual(
            self._parse("CONFIG_ADDR=0xdeadbeef\n"),
            {"ADDR": "0xdeadbeef"},
        )

    def test_string_strips_quotes(self):
        self.assertEqual(
            self._parse('CONFIG_S="hello world"\n'),
            {"S": "hello world"},
        )

    def test_string_unescape(self):
        self.assertEqual(
            self._parse(r'CONFIG_S="a\"b\\c"' + "\n"),
            {"S": 'a"b\\c'},
        )

    def test_mixed(self):
        got = self._parse(textwrap.dedent("""\
            # Kconfig-style header
            CONFIG_A=y
            # CONFIG_B is not set
            CONFIG_HZ=500
            CONFIG_NAME="xyz"
        """))
        self.assertEqual(
            got,
            {"A": "y", "B": "n", "HZ": "500", "NAME": "xyz"},
        )

    def test_blank_lines_ignored(self):
        self.assertEqual(
            self._parse("\n\nCONFIG_X=y\n\n"),
            {"X": "y"},
        )


class TestParseCmakeCache(unittest.TestCase):

    def _parse(self, text: str) -> dict[str, tuple[str, str]]:
        with tempfile.NamedTemporaryFile(
            mode="w", delete=False, suffix=".txt"
        ) as f:
            f.write(text)
            path = Path(f.name)
        try:
            return apply_config.parse_cmakecache(path)
        finally:
            path.unlink()

    def test_basic(self):
        got = self._parse(textwrap.dedent("""\
            // Something
            //Help on next line
            FOO:BOOL=ON
            BAR:STRING=hello
            HZ:STRING=250
        """))
        self.assertEqual(got["FOO"], ("BOOL", "ON"))
        self.assertEqual(got["BAR"], ("STRING", "hello"))
        self.assertEqual(got["HZ"], ("STRING", "250"))

    def test_comments_ignored(self):
        got = self._parse("# a comment\n// also a comment\nX:BOOL=OFF\n")
        self.assertEqual(set(got.keys()), {"X"})


class TestBoolCanonical(unittest.TestCase):

    def test_truthy(self):
        for v in ("y", "Y", "yes", "ON", "on", "TRUE", "true", "1"):
            self.assertEqual(apply_config.bool_canonical(v), "y", v)

    def test_falsy(self):
        for v in ("n", "NO", "off", "False", "0", ""):
            self.assertEqual(apply_config.bool_canonical(v), "n", v)

    def test_not_a_bool(self):
        for v in ("maybe", "250", "0xff", "hello"):
            self.assertIsNone(apply_config.bool_canonical(v), v)


class TestValuesMatch(unittest.TestCase):

    def test_bool_same_normalisation(self):
        self.assertTrue(apply_config.values_match("y", ("BOOL", "ON")))
        self.assertTrue(apply_config.values_match("n", ("BOOL", "OFF")))
        self.assertTrue(apply_config.values_match("y", ("BOOL", "1")))

    def test_bool_different(self):
        self.assertFalse(apply_config.values_match("y", ("BOOL", "OFF")))
        self.assertFalse(apply_config.values_match("n", ("BOOL", "ON")))

    def test_string_match(self):
        self.assertTrue(apply_config.values_match("abc", ("STRING", "abc")))

    def test_int_match(self):
        self.assertTrue(apply_config.values_match("250", ("STRING", "250")))
        self.assertFalse(apply_config.values_match("250", ("STRING", "500")))


class TestComputeDflags(unittest.TestCase):

    def test_no_changes_returns_empty(self):
        dc = {"A": "y", "B": "250", "C": "hello"}
        cc = {
            "A": ("BOOL", "ON"),
            "B": ("STRING", "250"),
            "C": ("STRING", "hello"),
        }
        self.assertEqual(apply_config.compute_dflags(dc, cc), [])

    def test_bool_flip_emits_single_dflag(self):
        dc = {"A": "n"}
        cc = {"A": ("BOOL", "ON")}
        self.assertEqual(apply_config.compute_dflags(dc, cc), ["-DA=0"])

    def test_bool_emitted_as_1_not_on(self):
        dc = {"A": "y"}
        cc = {"A": ("BOOL", "OFF")}
        self.assertEqual(apply_config.compute_dflags(dc, cc), ["-DA=1"])

    def test_string_change(self):
        dc = {"S": "world"}
        cc = {"S": ("STRING", "hello")}
        self.assertEqual(apply_config.compute_dflags(dc, cc), ["-DS=world"])

    def test_int_change(self):
        dc = {"HZ": "500"}
        cc = {"HZ": ("STRING", "250")}
        self.assertEqual(apply_config.compute_dflags(dc, cc), ["-DHZ=500"])

    def test_dotconfig_entry_missing_from_cache_is_skipped(self):
        dc = {"ONLY_IN_DC": "y"}
        cc: dict[str, tuple[str, str]] = {}
        self.assertEqual(apply_config.compute_dflags(dc, cc), [])

    def test_cache_entry_missing_from_dotconfig_is_ignored(self):
        dc = {"A": "y"}
        cc = {
            "A": ("BOOL", "ON"),
            "ONLY_IN_CACHE": ("BOOL", "ON"),
        }
        self.assertEqual(apply_config.compute_dflags(dc, cc), [])

    def test_multiple_changes(self):
        dc = {"A": "y", "B": "n", "HZ": "500"}
        cc = {
            "A": ("BOOL", "OFF"),
            "B": ("BOOL", "ON"),
            "HZ": ("STRING", "250"),
        }
        # Order is dict iteration order (insertion in Py 3.7+).
        self.assertEqual(
            apply_config.compute_dflags(dc, cc),
            ["-DA=1", "-DB=0", "-DHZ=500"],
        )


if __name__ == "__main__":
    unittest.main()
