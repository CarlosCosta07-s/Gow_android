#!/usr/bin/env python3
"""Validate the prepared Waffle Android core-only lifecycle contract."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime-dir", required=True, type=Path)
    args = parser.parse_args()

    root = args.runtime_dir.expanduser().resolve()
    header_path = root / "ps2xRuntime/include/ps2_runtime.h"
    runtime_path = root / "ps2xRuntime/src/lib/ps2_runtime.cpp"
    gs_path = root / "ps2xRuntime/src/lib/gs/gs_frontend.cpp"

    for path in (header_path, runtime_path, gs_path):
        require(path.is_file(), f"Prepared runtime file is missing: {path}")

    header = header_path.read_text(encoding="utf-8")
    runtime = runtime_path.read_text(encoding="utf-8")
    gs = gs_path.read_text(encoding="utf-8")

    require(
        "bool initializeCoreHeadless();" in header
        and "bool m_coreInitialized = false;" in header,
        "PS2Runtime headless core API/state was not prepared.",
    )

    start = runtime.find("bool PS2Runtime::initializeCoreHeadless()")
    end = runtime.find("bool PS2Runtime::initialize(const char *title)", start)
    require(start >= 0 and end > start, "Headless core initializer was not prepared.")
    core_body = runtime[start:end]

    for required in ("m_memory.initialize()", "syncCoreSubsystems()", "m_coreInitialized = true"):
        require(required in core_body, f"Headless core initializer is missing: {required}")

    for forbidden in (
        "InitWindow(",
        "InitAudioDevice(",
        "captureHostContext(",
        "startSpu2Audio(",
        "renderFrame(",
    ):
        require(forbidden not in core_body, f"Headless core initializer touches host API: {forbidden}")

    constructor_start = gs.find("GS::GS()")
    constructor_end = gs.find("void GS::init(", constructor_start)
    require(
        constructor_start >= 0 and constructor_end > constructor_start,
        "GS constructor was not found in the prepared source.",
    )
    constructor = gs[constructor_start:constructor_end]
    require(
        "#if defined(PS2X_ANDROID_LIFECYCLE_MANAGED)" in constructor
        and "std::make_unique<GSNullBackend>()" in constructor
        and "GSThreadedBackend::MakeDefault()" in constructor,
        "Android GS null backend was not selected conditionally.",
    )

    initialize_guard = runtime[end:runtime.find("void PS2Runtime::shutdownHostGraphics()", end)]
    require(
        "PS2X_ANDROID_LIFECYCLE_MANAGED" in initialize_guard
        and "desktop host path is disabled on Android" in initialize_guard,
        "Desktop Raylib initialization is not blocked in the Android profile.",
    )

    run_start = runtime.find("void PS2Runtime::run()")
    run_end = runtime.find("\n}", run_start)
    require(run_start >= 0 and run_end > run_start, "PS2Runtime::run was not found.")
    run_prefix = runtime[run_start:run_start + 900]
    require(
        "PS2X_ANDROID_LIFECYCLE_MANAGED" in run_prefix
        and "guest code" in run_prefix,
        "Guest execution is not guarded in the Android profile.",
    )

    project_root = Path(__file__).resolve().parents[1]
    native_main_path = project_root / "android/app/src/main/cpp/main.cpp"
    activity_path = project_root / "android/app/src/main/java/org/gowandroid/app/GowGameActivity.java"
    require(native_main_path.is_file(), "Android native entry point is missing.")
    require(activity_path.is_file(), "GameActivity Java source is missing.")
    native_main = native_main_path.read_text(encoding="utf-8")
    activity = activity_path.read_text(encoding="utf-8")

    for required in (
        "#include <jni.h>",
        "AttachCurrentThread",
        'GetMethodID(activityClass, "onNativeRuntimeStatus"',
        '"(Ljava/lang/String;Z)V"',
        "CallVoidMethod",
        "DetachCurrentThread",
        "NewGlobalRef(activity)",
        "NewLocalRef(gActivityReference)",
        "Java_org_gowandroid_app_GowGameActivity_nativeSetActivityReference",
        "Java_org_gowandroid_app_GowGameActivity_nativeClearActivityReference",
        "IsSameObject(gActivityReference, activity)",
    ):
        require(required in native_main, f"JNI runtime-status bridge is missing: {required}")
    require(
        "app->activity->clazz" not in native_main,
        "GameActivity status bridge must not rely on NativeActivity::clazz.",
    )

    require(
        native_main.count("reportRuntimeStatus(") >= 5
        and "Preparando o núcleo PS2" in native_main
        and "Falha ao inicializar o núcleo PS2" in native_main,
        "Native startup does not report progress and initialization failures.",
    )
    for required in (
        "nativeSetActivityReference(this)",
        "nativeClearActivityReference(this)",
        "nativeClearActivityReference(GowGameActivity activity)",
        "protected void onDestroy()",
        "createNativeStatusOverlay()",
        "onNativeRuntimeStatus(String message, boolean isError)",
        "runOnUiThread(() ->",
        "nativeStatusView.setVisibility(View.VISIBLE)",
        "createStatusBackground(isError)",
    ):
        require(required in activity, f"GameActivity diagnostic overlay is missing: {required}")

    runtime_bridge_path = project_root / "android/app/src/main/cpp/runtime_link_bridge.cpp"
    require(runtime_bridge_path.is_file(), "Android runtime link bridge is missing.")
    runtime_bridge = runtime_bridge_path.read_text(encoding="utf-8")
    load_start = runtime_bridge.find("extern \\"C\\" bool gowRuntimeLoadElfAndCheckAot(")
    load_end = runtime_bridge.find("extern \\"C\\" bool gowRuntimeValidateIopCdvd(", load_start)
    require(
        load_start >= 0 and load_end > load_start,
        "Guarded private ELF/AOT loader bridge is missing.",
    )
    load_body = runtime_bridge[load_start:load_end]
    for required in (
        "g_ps2RecompiledFunctionTableSlotCount == 0u",
        "runtime->loadELF(candidate.string())",
        "runtime->cpu().pc",
        "runtime->hasFunction(entryPoint)",
        "std::filesystem::weakly_canonical",
        "guest execution ainda bloqueada",
    ):
        require(required in load_body, f"Private ELF/AOT loader gate is missing: {required}")
    require(
        "runtime->run(" not in load_body and "PS2Runtime::run(" not in load_body,
        "Private ELF/AOT validation must never start guest execution.",
    )

    print("Verified: Android PS2 core initialization is headless, uses a null GS backend,")
    print("and cannot enter Raylib desktop initialization or guest execution.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"Android runtime lifecycle check failed: {error}", file=sys.stderr)
        raise SystemExit(1)
