from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from build_android_game import _verify_generated_outputs, create_build_plan


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

    def _make_generated_output(self, registration_source: str) -> Path:
        generated = self.root / "android/app/build/gow-generated-game"
        (generated / "functions").mkdir(parents=True, exist_ok=True)
        (generated / "register_functions.cpp").write_text(
            registration_source, encoding="utf-8"
        )
        (generated / "ps2_recompiled_functions.h").write_text(
            "#pragma once\\n", encoding="utf-8"
        )
        (generated / "ps2_recompiled_stubs.h").write_text(
            "#pragma once\\n", encoding="utf-8"
        )
        (generated / "functions/boot.cpp").write_text(
            "void gow_boot() {}\\n", encoding="utf-8"
        )
        return generated

    @staticmethod
    def _valid_registration_source() -> str:
        return """#include "ps2_runtime.h"
extern const uint32_t g_ps2RecompiledFunctionTableBase = 0x00100000u;
extern const uint32_t g_ps2RecompiledFunctionTableEnd = 0x00100008u;
extern const uint32_t g_ps2RecompiledFunctionTableSlotCount = 2u;
PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[2u] = {};
namespace {
struct GeneratedFunctionTableInitializer {
    GeneratedFunctionTableInitializer() {
        g_ps2RecompiledFunctionTable[0] = gow_boot; // 0x100000
        g_ps2RecompiledFunctionTable[1] = gow_tick; // 0x100004
    }
};
static const GeneratedFunctionTableInitializer g_generatedFunctionTableInitializer;
}
"""

    def test_aot_verifier_accepts_entrypoint_and_registered_functions(self) -> None:
        generated = self._make_generated_output(self._valid_registration_source())
        summary = _verify_generated_outputs(
            generated, expected_entry_point=0x00100000
        )
        self.assertEqual(summary.translation_units, 2)
        self.assertEqual(summary.table_slots, 2)
        self.assertEqual(summary.registered_functions, 2)
        self.assertEqual(summary.entry_point, 0x00100000)

    def test_aot_verifier_rejects_empty_function_table(self) -> None:
        source = """#include "ps2_runtime.h"
extern const uint32_t g_ps2RecompiledFunctionTableBase = 0x00100000u;
extern const uint32_t g_ps2RecompiledFunctionTableEnd = 0x00100000u;
extern const uint32_t g_ps2RecompiledFunctionTableSlotCount = 0u;
PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[1u] = {};
"""
        generated = self._make_generated_output(source)
        with self.assertRaisesRegex(RuntimeError, "function table is empty"):
            _verify_generated_outputs(generated, expected_entry_point=0x00100000)

    def test_aot_verifier_rejects_missing_entrypoint(self) -> None:
        generated = self._make_generated_output(self._valid_registration_source())
        with self.assertRaisesRegex(RuntimeError, "does not cover the ELF entry point"):
            _verify_generated_outputs(
                generated, expected_entry_point=0x00100008
            )

    def test_aot_verifier_rejects_wrong_slot_mapping(self) -> None:
        source = self._valid_registration_source().replace(
            "g_ps2RecompiledFunctionTable[0] = gow_boot; // 0x100000",
            "g_ps2RecompiledFunctionTable[1] = gow_boot; // 0x100000",
        )
        generated = self._make_generated_output(source)
        with self.assertRaisesRegex(RuntimeError, "maps 0x00100000 to slot 1"):
            _verify_generated_outputs(generated, expected_entry_point=0x00100000)

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

    def test_rejects_pal_executable_for_ntsc_u_map(self) -> None:
        pal_elf = self.private / "SCES_531.33"
        pal_elf.write_bytes(b"synthetic test ELF placeholder")
        with self.assertRaisesRegex(ValueError, "pinned to the NTSC-U"):
            create_build_plan(
                self.root, pal_elf, self.work, windows=False,
            )

    def test_rejects_missing_elf(self) -> None:
        with self.assertRaisesRegex(ValueError, "was not found"):
            create_build_plan(
                self.root, self.private / "missing.elf", self.work,
                windows=False,
            )


if __name__ == "__main__":
    unittest.main()
