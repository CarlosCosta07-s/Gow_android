from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from build_android_game import create_build_plan


class AndroidGameBuildPlanTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="gow-android-plan-")
        self.root = Path(self.temporary.name) / "repo"
        self.root.mkdir()
        (self.root / "tools").mkdir()
        (self.root / "tools/build_recompiler_host.py").write_text("# test", encoding="utf-8")
        (self.root / "android/app/build").mkdir(parents=True)
        (self.root / "gradlew").write_text("#!/bin/sh\n", encoding="utf-8")
        self.private = Path(self.temporary.name) / "private"
        self.private.mkdir()
        self.elf = self.private / "SCUS_973.99"
        self.elf.write_bytes(b"synthetic test ELF placeholder")
        self.work = self.private / "gowport"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_plan_keeps_elf_workspace_and_generated_output_separate(self) -> None:
        plan = create_build_plan(
            self.root, self.elf, self.work,
            python_executable="python-test", windows=False,
        )
        self.assertEqual(plan.elf, self.elf.resolve())
        self.assertEqual(plan.work_dir, self.work.resolve())
        self.assertEqual(
            plan.generated_dir,
            (self.root / "android/app/build/gow-generated-game").resolve(),
        )
        self.assertIn("--export-game-cpp-dir", plan.recompiler_command)
        self.assertIn(str(plan.generated_dir), plan.recompiler_command)
        self.assertEqual(plan.gradle_command[-1], "assembleDebug")
        self.assertTrue(str(plan.apk_path).endswith("outputs/apk/debug/app-debug.apk"))

    def test_rejects_elf_inside_repository(self) -> None:
        public_elf = self.root / "game.elf"
        public_elf.write_bytes(b"not allowed")
        with self.assertRaisesRegex(ValueError, "outside the public repository"):
            create_build_plan(self.root, public_elf, self.work, windows=False)

    def test_rejects_workspace_inside_repository(self) -> None:
        with self.assertRaisesRegex(ValueError, "outside the Git repository"):
            create_build_plan(
                self.root, self.elf, self.root / "android/app/build/tmp",
                windows=False,
            )

    def test_rejects_non_ascii_workspace_path(self) -> None:
        non_ascii = self.private / "área-privada"
        with self.assertRaisesRegex(ValueError, "ASCII characters"):
            create_build_plan(self.root, self.elf, non_ascii, windows=False)

    def test_windows_plan_uses_cmd_for_gradle_batch_wrapper(self) -> None:
        (self.root / "gradlew.bat").write_text("@echo off\\r\\n", encoding="utf-8")
        plan = create_build_plan(
            self.root, self.elf, self.work,
            python_executable="python-test", windows=True,
        )
        self.assertEqual(plan.gradle_command[:2], ("cmd", "/c"))
        self.assertTrue(plan.gradle_command[2].endswith("gradlew.bat"))
        self.assertEqual(plan.gradle_command[-1], "assembleDebug")

    def test_rejects_missing_elf(self) -> None:
        with self.assertRaisesRegex(ValueError, "was not found"):
            create_build_plan(
                self.root, self.private / "missing.elf", self.work,
                windows=False,
            )


if __name__ == "__main__":
    unittest.main()
