#!/usr/bin/env python3
"""Exercise the Hosted patch lifecycle without touching managed_components."""

import importlib.util
import difflib
import hashlib
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "tools/prepare_esp_hosted_cpu_staging.py"
SPEC = importlib.util.spec_from_file_location("hosted_staging", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class HostedStagingTests(unittest.TestCase):
    def setUp(self):
        self.sandbox = tempfile.TemporaryDirectory()
        self.addCleanup(self.sandbox.cleanup)
        self.component = Path(self.sandbox.name)
        self.paths = (Path("src/first.c"), Path("src/second.c"))
        original = ("int first = 1;\n", "int second = 2;\n")
        changed = ("int first = 3;\n", "int second = 4;\n")
        self.base = tuple(hashlib.sha256(text.encode()).hexdigest() for text in original)
        self.staged = tuple(hashlib.sha256(text.encode()).hexdigest() for text in changed)
        self.patch_file = self.component / "staging.patch"
        self.patch_file.write_text("".join(
            "".join(difflib.unified_diff(before.splitlines(keepends=True), after.splitlines(keepends=True),
                                         fromfile=f"a/{relative}", tofile=f"b/{relative}"))
            for relative, before, after in zip(self.paths, original, changed)
        ))
        (self.component / "idf_component.yml").write_text(
            "version: 3.0.10\nrepository_info:\n"
            "  commit_sha: 2e3f6c8bd717d67698319a6a24f9f4218d8db068\n"
        )
        for relative, text in zip(self.paths, changed):
            target = self.component / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(text)
        self.assertEqual(self.hashes(), self.staged)

    def hashes(self):
        return tuple(MODULE.digest(self.component / relative) for relative in self.paths)

    def run_action(self, action):
        with patch.multiple(MODULE, COMPONENT=self.component, FILES=self.paths,
                            PATCH=self.patch_file, BASE=self.base, STAGED=self.staged), \
                patch.object(sys, "argv", [str(SCRIPT), action]):
            return MODULE.main()

    def test_restore_apply_and_verify_round_trip(self):
        self.assertEqual(self.run_action("verify"), 0)
        self.assertEqual(self.run_action("restore"), 0)
        self.assertEqual(self.hashes(), self.base)
        self.assertEqual(self.run_action("apply"), 0)
        self.assertEqual(self.hashes(), self.staged)
        self.assertEqual(self.run_action("verify"), 0)

    def test_unknown_source_is_unchanged(self):
        target = self.component / self.paths[0]
        with target.open("ab") as file:
            file.write(b"\nunknown change\n")
        before = self.hashes()
        with self.assertRaises(SystemExit):
            self.run_action("restore")
        self.assertEqual(self.hashes(), before)

    def test_version_mismatch_is_unchanged(self):
        (self.component / "idf_component.yml").write_text("version: 3.0.11\n")
        before = self.hashes()
        with self.assertRaises(SystemExit):
            self.run_action("restore")
        self.assertEqual(self.hashes(), before)

    def test_configure_requires_staged_sources(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn('project(cyberdeck5)', cmake)
        self.assertIn('prepare_esp_hosted_cpu_staging.py" verify', cmake)
        self.assertIn('if(NOT hosted_staging_result EQUAL 0)', cmake)
        self.assertIn('add_custom_target(verify_esp_hosted_cpu_staging ALL', cmake)
        self.assertIn('add_dependencies(${CMAKE_PROJECT_NAME}.elf verify_esp_hosted_cpu_staging)', cmake)


if __name__ == "__main__":
    unittest.main()
