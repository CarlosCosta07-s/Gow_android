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

    project_root = Path(__file__).resolve().parents[1]
    vu1_compat_source = project_root / "port/kiexter/vu1/ps2_vu1_neon_compat.h"
    vu1_mapping_source = project_root / "port/kiexter/vu1/ps2_vu1_fpcr_mapping.h"
    vu1_compat_target = root / "ps2xRuntime/include/runtime/ps2_vu1_neon_compat.h"
    vu1_mapping_target = root / "ps2xRuntime/include/runtime/ps2_vu1_fpcr_mapping.h"
    for source, target, description in (
        (vu1_compat_source, vu1_compat_target, "NEON compatibility header"),
        (vu1_mapping_source, vu1_mapping_target, "FPCR mapping header"),
    ):
        require(source.is_file(), f"Repository {description} is missing: {source}")
        require(target.is_file(), f"Prepared runtime {description} is missing: {target}")
        require(
            source.read_bytes() == target.read_bytes(),
            f"Prepared runtime {description} differs from the repository source.",
        )

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

    # XYZF writes the vertex F attribute and also updates the persistent GS
    # FOG latch, which subsequent XYZ (without F) inherit. Keep all three
    # decoder paths covered: PACKED XYZF2, PACKED XYZF3, and A+D/REGLIST.
    require(
        "        vtx.fog = f;\n"
        "        m_curFog = f;\n"
        "        vertexKick(!adk);\n" in gs,
        "PACKED XYZF2 does not update the persistent GS FOG latch.",
    )
    require(
        "        const uint8_t fog = static_cast<uint8_t>((hi >> 36) & 0xFF);\n"
        "        vtx.fog = fog;\n"
        "        m_curFog = fog;\n"
        "        vertexKick(false);\n" in gs,
        "PACKED XYZF3 does not update the persistent GS FOG latch.",
    )
    require(
        "        const uint8_t fog = static_cast<uint8_t>((value >> 56) & 0xFF);\n"
        "        vtx.fog = fog;\n"
        "        m_curFog = fog;\n"
        "        vtx.r = m_curR;\n" in gs,
        "A+D/REGLIST XYZF2/XYZF3 do not update the persistent GS FOG latch.",
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

    require(
        "std::thread m_androidGuestThread;" in header
        and "bool startGuestExecutionAndroid();" in header
        and "void stopGuestExecutionAndroid() noexcept;" in header,
        "Android guest worker API or thread ownership state is missing.",
    )
    worker_start = runtime.find("bool PS2Runtime::startGuestExecutionAndroid()")
    worker_end = runtime.find("void PS2Runtime::run()", worker_start)
    require(
        worker_start >= 0 and worker_end > worker_start,
        "Dedicated Android guest worker was not prepared.",
    )
    worker_body = runtime[worker_start:worker_end]
    for required in (
        "m_coreInitialized",
        "!m_eeScheduler",
        "hasFunction(m_cpuContext.pc)",
        "m_eeScheduler->reset(",
        "m_eeScheduler->run()",
        "std::thread([this]()",
        "m_androidGuestFinished.store(true",
    ):
        require(required in worker_body, f"Android guest worker is missing: {required}")
    for forbidden in (
        "GenImageColor(",
        "LoadTextureFromImage(",
        "BeginDrawing(",
        "EndDrawing(",
        "InitWindow(",
        "CloseWindow(",
    ):
        require(forbidden not in worker_body, f"Android guest worker touches Raylib presentation: {forbidden}")
    stop_start = runtime.find("void PS2Runtime::stopGuestExecutionAndroid()")
    stop_end = runtime.find("bool PS2Runtime::isGuestExecutionFinishedAndroid()", stop_start)
    stop_body = runtime[stop_start:stop_end]
    require(
        stop_start >= 0 and stop_end > stop_start
        and "requestStop()" in stop_body
        and "m_androidGuestThread.join()" in stop_body
        and "std::terminate()" in stop_body
        and ".detach()" not in stop_body,
        "Android guest worker shutdown must request stop, join, and never detach a thread that can still access the runtime.",
    )
    require(
        "stopGuestExecutionAndroid();" in runtime[
            runtime.find("PS2Runtime::~PS2Runtime()"):
            runtime.find("ps2x::iop::ModuleLoadResult PS2Runtime::loadIopModule")
        ],
        "PS2Runtime destructor does not stop the Android guest worker before destroying subsystems.",
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
    aot_ready_start = runtime_bridge.find('extern "C" bool gowRuntimeHasPrivateAotTable()')
    aot_ready_end = runtime_bridge.find("\n}\n", aot_ready_start)
    require(
        aot_ready_start >= 0 and aot_ready_end > aot_ready_start,
        "Early private AOT readiness bridge is missing.",
    )
    aot_ready_body = runtime_bridge[aot_ready_start:aot_ready_end]
    require(
        "g_ps2RecompiledFunctionTableSlotCount != 0u" in aot_ready_body
        and "g_ps2RecompiledFunctionTable != nullptr" in aot_ready_body,
        "Early AOT readiness bridge does not validate the generated function table.",
    )
    load_start = runtime_bridge.find('extern "C" bool gowRuntimeLoadElfAndCheckAot(')
    load_end = runtime_bridge.find('extern "C" bool gowRuntimeValidateIopCdvd(', load_start)
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
        "gow::disc::resolvePrivateBootElfPath",
        "guest execution ainda bloqueada",
    ):
        require(required in load_body, f"Private ELF/AOT loader gate is missing: {required}")
    private_path_source = project_root / "android/app/src/main/cpp/private_boot_path.cpp"
    private_path_test = project_root / "android/app/src/test/cpp/private_boot_path_test.cpp"
    require(
        private_path_source.is_file() and private_path_test.is_file(),
        "Private BOOT2 path resolver or its host test is missing.",
    )
    private_path_code = private_path_source.read_text(encoding="utf-8")
    for required in (
        "input.front() == '/'",
        "component == \"..\"",
        "isStrictChildPath",
        "std::filesystem::is_regular_file(candidate",
        "weakly_canonical",
    ):
        require(required in private_path_code, f"Private BOOT2 path safety rule is missing: {required}")

    for required in (
        "Java_org_gowandroid_app_GowGameActivity_nativeConfigurePrivateBoot",
        "gBootPathCondition.wait(",
        "gBootPathCondition.notify_all()",
        "gBootPathConfigurationReceived",
        "gBootRelativePath.clear()",
        "gBootPathConfigurationReceived = false",
        "gowRuntimeLoadElfAndCheckAot(",
        "gBootRelativePath = rootReady ? std::move(candidate) : std::string{}",
    ):
        require(required in native_main, f"GameActivity BOOT2 handoff is missing: {required}")
    android_main_body = native_main[
        native_main.find('extern "C" void android_main'):
    ]
    require(
        "gowRuntimeLoadElfAndCheckAot(" in android_main_body,
        "android_main must retain the guarded ELF/AOT loader for private AOT builds.",
    )
    aot_gate_position = android_main_body.find(
        "gPrivateAotTableAvailable = gowRuntimeHasPrivateAotTable();"
    )
    runtime_create_position = android_main_body.find("runtime = gowCreatePs2Runtime();")
    require(
        aot_gate_position >= 0 and runtime_create_position > aot_gate_position
        and "if (!gPrivateAotTableAvailable)" in android_main_body
        and "native core and Vulkan startup were skipped" in android_main_body,
        "The shell-only APK must skip unvalidated PS2 core startup when its AOT table is empty.",
    )
    window_handler_start = native_main.find("void handleAppCommand(")
    window_handler_end = native_main.find("} // namespace", window_handler_start)
    window_handler = native_main[window_handler_start:window_handler_end]
    init_window_start = window_handler.find("case APP_CMD_INIT_WINDOW:")
    term_window_start = window_handler.find("case APP_CMD_TERM_WINDOW:", init_window_start)
    init_window_body = window_handler[init_window_start:term_window_start]
    require(
        "if (!gPrivateAotTableAvailable)" in init_window_body
        and init_window_body.find("if (!gPrivateAotTableAvailable)")
            < init_window_body.find("gRenderer.initialize(app->window)"),
        "The shell-only APK must not initialize Vulkan without private AOT code.",
    )
    require(
        "runtime->run(" not in native_main,
        "Android native startup must not execute guest code before GS/audio are ready.",
    )

    # The first-boot diagnostic may start the worker only after the private ELF
    # has passed the AOT entry-point gate and the Android GS/Vulkan path is live.
    start_bridge = runtime_bridge.find(
        'extern "C" bool gowRuntimeStartGuestExecutionAndroid('
    )
    finish_bridge = runtime_bridge.find(
        'extern "C" bool gowRuntimeGuestExecutionFinishedAndroid('
    )
    cdvd_bridge = runtime_bridge.find(
        'extern "C" bool gowRuntimeValidateIopCdvd(',
        finish_bridge,
    )
    require(
        start_bridge >= 0 and finish_bridge > start_bridge
        and cdvd_bridge > finish_bridge,
        "Gated Android guest start/finish bridge is missing.",
    )
    start_bridge_body = runtime_bridge[start_bridge:finish_bridge]
    finish_bridge_body = runtime_bridge[finish_bridge:cdvd_bridge]
    require(
        "runtime->startGuestExecutionAndroid()" in start_bridge_body
        and "runtime->isGuestExecutionFinishedAndroid()" in finish_bridge_body,
        "Android worker bridge does not call the prepared runtime worker API.",
    )

    for required in (
        "bool gPrivateAotElfReady = false;",
        "gPrivateAotElfReady = elfReady;",
        "bool tryStartAndroidGuestExecution(android_app *app)",
        "!gPrivateAotElfReady",
        "!gGsBackendAttached",
        "!gRenderer.isReady()",
        "gowRuntimeStartGuestExecutionAndroid(",
        "void reportAndroidGuestCompletion(android_app *app)",
        "gowRuntimeGuestExecutionFinishedAndroid(",
        "tryStartAndroidGuestExecution(app)",
    ):
        require(required in native_main, f"Gated Android first-boot path is missing: {required}")
    require(
        "startGuestExecutionAndroid(" not in native_main
        and "runtime->run(" not in native_main,
        "android_main must use the gated worker bridge, never the desktop run() path.",
    )

    require(
        "runtime->run(" not in load_body and "PS2Runtime::run(" not in load_body,
        "Private ELF/AOT validation must never start guest execution.",
    )

    for required in (
        "EXTRA_PRIVATE_CD_ROOT",
        "EXTRA_BOOT_RELATIVE_PATH",
        "nativeConfigurePrivateBoot(privateCdRoot, bootRelativePath)",
        "nativeConfigurePrivateBoot(\"\", \"\")",
    ):
        require(required in activity, f"GameActivity private disc handoff is missing: {required}")
    main_activity_path = project_root / "android/app/src/main/java/org/gowandroid/app/MainActivity.java"
    main_activity = main_activity_path.read_text(encoding="utf-8")
    for required in (
        "openRuntimeDiagnostics()",
        "GowGameActivity.EXTRA_PRIVATE_CD_ROOT",
        "GowGameActivity.EXTRA_BOOT_RELATIVE_PATH",
        "startActivity(intent)",
    ):
        require(required in main_activity, f"FORGE diagnostic launch is missing: {required}")

    # Do not enter GameActivity from the public shell APK when no private
    # recompilation table is linked; explain the missing AOT while preserving
    # the already-verified disc installation.
    for required in (
        "private static native boolean nativeHasPrivateAotTable();",
        "if (!nativeHasPrivateAotTable())",
        "Código nativo do jogo ainda não preparado",
        "código recompilado do jogo",
        "modo --installed-android",
    ):
        require(required in main_activity, f"FORGE missing-AOT user guard is missing: {required}")
    require(
        main_activity.count('(dialog, which) -> onPlayPressed())') == 2
        and '(dialog, which) -> openRuntimeDiagnostics())' not in main_activity,
        "A JOGAR dialog is bypassing the private AOT availability guard.",
    )
    require(
        "Java_org_gowandroid_app_MainActivity_nativeHasPrivateAotTable" in native_main
        and "gowRuntimeHasPrivateAotTable() ? JNI_TRUE : JNI_FALSE" in native_main,
        "FORGE native AOT availability query is missing.",
    )

    # Once installation checks pass, the primary action must become a real
    # first-boot attempt instead of looping through the same verification flow.
    for required in (
        "private boolean installationReadyForBoot;",
        "if (installationReadyForBoot) {",
        "installationReadyForBoot = installationValid && !cancelled;",
        "installationReadyForBoot = success;",
        'installDiscButton.setText(installationReadyForBoot ? "JOGAR" : "VERIFICAR DISCO");',
        '"JOGAR",',
        "tabela AOT privada gerada a partir da ISO",
    ):
        require(required in main_activity, f"FORGE post-validation play flow is missing: {required}")

    # An already-installed private disc must remain discoverable even when a
    # Debug package migration has cleared the original Storage Access Framework URI.
    for required in (
        "if (!restorePrivateInstallationIfAvailable())",
        "restoreSelectedDisc()",
        "private boolean restorePrivateInstallationIfAvailable()",
        '"gow_scus_973_99_ntsc_u"',
        '"SYSTEM.CNF"',
        "parseSystemCnfBootPath(",
        'selectedDiscSerial = "SCUS_973.99 (NTSC-U)"',
        '"VERIFICAR DISCO"',
        "SYSTEM.CNF privado foi truncado.",
        "showExistingInstallationVerification(",
        "verifyExistingInstallationAsync(installation, bootPath)",
        "selectedDiscUri == null",
        "import java.io.FileInputStream;",
    ):
        require(required in main_activity, f"Private installed-disc recovery is missing: {required}")
    private_restore_start = main_activity.find(
        "private boolean restorePrivateInstallationIfAvailable()"
    )
    private_restore_end = main_activity.find(
        "private void validateAndDisplay(", private_restore_start
    )
    private_restore = main_activity[private_restore_start:private_restore_end]
    require(
        "new File(getFilesDir(), \"installations\")" in private_restore
        and "getFilesDir()" in private_restore
        and "selectedDiscBootPath = bootPath" in private_restore
        and "return true;" in private_restore
        and "return false;" in private_restore,
        "Private installed-disc recovery does not restore its BOOT2 path and success state.",
    )
    validate_start = main_activity.find("private void validateAndDisplay(Uri uri)")
    validate_end = main_activity.find("private String identifyGodOfWarDisc(", validate_start)
    validate_body = main_activity[validate_start:validate_end]
    require(
        "installationDirectoryName(finalSerial)" in validate_body
        and 'alreadyInstalled ? "VERIFICAR DISCO" : "INSTALAR JOGO"' in validate_body,
        "Restoring a selected ISO must detect an existing private installation instead of offering install again.",
    )

    on_create_start = activity.find("protected void onCreate(Bundle savedInstanceState)")
    on_create_super = activity.find("super.onCreate(savedInstanceState)", on_create_start)
    on_create_register = activity.find("nativeSetActivityReference(this)", on_create_start)
    require(
        on_create_start >= 0 and on_create_register >= 0
        and on_create_register < on_create_super,
        "GameActivity must register the JNI reference before super.onCreate starts the native thread.",
    )
    cmake_path = project_root / "android/app/src/main/cpp/CMakeLists.txt"
    cmake_text = cmake_path.read_text(encoding="utf-8")
    gradle_path = project_root / "android/app/build.gradle"
    gradle_text = gradle_path.read_text(encoding="utf-8")
    gs_renderer_source = project_root / "android/app/src/main/cpp/main.cpp"
    gs_renderer = gs_renderer_source.read_text(encoding="utf-8")
    gs_backend_path = project_root / "android/app/src/main/cpp/android_gs_vulkan_backend.cpp"
    gs_geometry_path = project_root / "android/app/src/main/cpp/android_gs_geometry.cpp"
    require(
        gs_backend_path.is_file() and gs_geometry_path.is_file(),
        "Android GS backend adapter or geometry converter is missing.",
    )
    for required in (
        "android_gs_vulkan_backend.cpp",
        "android_gs_geometry.cpp",
        "GOW_ANDROID_ENABLE_GS_VULKAN",
        "gow_android_gs_shaders",
    ):
        require(required in cmake_text, f"GS Vulkan CMake integration is missing: {required}")
    for required in (
        "arguments \"-DGOW_ANDROID_ENABLE_GS_VULKAN=ON\"",
    ):
        require(required in gradle_text, f"Debug GS Vulkan build setting is missing: {required}")
    for required in (
        "vkCreateGraphicsPipelines",
        "vkCmdBindVertexBuffers",
        "vkCmdDraw(",
        "AndroidGSVulkanBackend",
        "attachAndroidGsBackend()",
        "detachAndroidGsBackend()",
        "enqueueGsBatch(",
    ):
        require(required in gs_renderer, f"Android GS Vulkan draw path is missing: {required}")

    # Black Android's frame-resource discipline and Dragon Rage's ADPF pattern
    # are adapted without importing either project's incompatible game renderer.
    performance_hint_header = project_root / "android/app/src/main/cpp/android_performance_hint.h"
    performance_hint_source = project_root / "android/app/src/main/cpp/android_performance_hint.cpp"
    require(
        performance_hint_header.is_file() and performance_hint_source.is_file(),
        "Android adaptive performance-hint module is missing.",
    )
    for required in (
        "android_performance_hint.cpp",
        "find_library(DL_LIBRARY dl REQUIRED)",
        "${DL_LIBRARY}",
    ):
        require(required in cmake_text, f"Android performance-hint CMake integration is missing: {required}")
    for required in (
        '#include "android_performance_hint.h"',
        "reportPerformanceHintFrame(",
        "performanceHintTargetDurationNs(",
        "accumulatedLooperWaitNs",
        "lastPresentedFrameAt",
        "vulkanWaitDurationNs",
        "nonLooperWorkNs > vulkanWaitDurationNs",
        "drawFrame(uint64_t &waitDurationNs)",
        "vkWaitForFences",
        "vkAcquireNextImageKHR",
        "frameVerticesScratch_.clear()",
        "frameVerticesScratch_.swap(queuedGsVertices_)",
        "frameDrawsScratch_.swap(queuedGsDraws_)",
    ):
        require(required in gs_renderer, f"Android sustained-performance/frame-buffer path is missing: {required}")
    require(
        "std::vector<gow::android::GsGpuVertex> frameVertices;" not in gs_renderer
        and "std::vector<QueuedGsDraw> frameDraws;" not in gs_renderer,
        "GS render-frame batches must use reusable member buffers instead of per-frame local allocations.",
    )
    for required in (
        "APerformanceHint_getManager",
        "APerformanceHint_createSession",
        "APerformanceHint_reportActualWorkDuration",
        "APerformanceHint_updateTargetWorkDuration",
        "GOW_ANDROID_ADPF",
        "kMaximumReportedWorkNs",
    ):
        require(required in performance_hint_source.read_text(encoding="utf-8"),
                f"Android ADPF integration is missing: {required}")
    require(
        "runtime->run(" not in gs_renderer,
        "The experimental GS renderer must not start guest execution.",
    )

    for required in (
        "nativeRequestGsSceneCapture",
        'setText("Capturar cena")',
        "ApplicationInfo.FLAG_DEBUGGABLE",
    ):
        require(required in activity, f"Debug GS scene capture control is missing: {required}")
    for required in (
        "Java_org_gowandroid_app_GowGameActivity_nativeRequestGsSceneCapture",
        "gGsSceneCaptureRequested.exchange(",
        "kMaxRequestedGsSceneCaptures",
        "gow_android_scene_%02u.ppm",
        "writeRgbaPpmCapture(",
    ):
        require(required in native_main, f"Local GS scene capture path is missing: {required}")

    # The strict Android worker must capture each loaded VU1 image before
    # rejecting a missing native block; IOP module images are dumped at load time.
    build_host = (project_root / "tools/build_recompiler_host.py").read_text(encoding="utf-8")
    for required in (
        'compiledProgramFor(vuCode, codeSize, memory)',
        'capture microcode before strict AOT lookup',
        'GOW_VU1N_MISS_LOG',
        'std::fopen(missLog, "a")',
        'std::fprintf(missFile, "%04x\\\\n", pc)',
        'PS2X_IOP_NATIVE_SOURCE_DIR',
        'route private generated IRX AOT files into the IOP target',
    ):
        require(required in build_host, f"Private AOT capture/build preparation is missing: {required}")

    for required in (
        "configurePrivateAotCaptureDirectories",
        'setenv("GOW_VU1_CAPTURA"',
        'setenv("GOW_VU1N_MISS_LOG"',
        'setenv("PS2X_IOP_DUMP_MODULES"',
        "gow-aot-captures",
    ):
        require(required in native_main, f"Private VU1/IOP capture setup is missing: {required}")

    for required in (
        "GOW_ANDROID_GENERATED_VU1_DIR",
        "GOW_ANDROID_GENERATED_IOP_DIR",
        "PS2X_IOP_NATIVE_SOURCE_DIR",
        "GOW_ANDROID_CAPTURE_BOOTSTRAP",
        "target_sources(ps2_runtime PRIVATE ${GOW_ANDROID_GENERATED_VU1_SOURCES})",
    ):
        require(required in cmake_text, f"Private AOT CMake integration is missing: {required}")

    prepared_iop_cmake = root / "ps2xIOP/CMakeLists.txt"
    require(prepared_iop_cmake.is_file(), "Prepared IOP CMake file is missing.")
    prepared_iop_text = prepared_iop_cmake.read_text(encoding="utf-8")
    for required in (
        "PS2X_IOP_NATIVE_SOURCE_DIR",
        "iopn_registry.cpp",
        "iopn_*.cpp",
    ):
        require(required in prepared_iop_text, f"Prepared IOP CMake AOT source path is missing: {required}")

    # Game-derived AOT captures are private implementation data. The Android
    # app must not expose document-picker or manual-export controls for them.
    for forbidden in (
        'setText("Exportar dados AOT")',
        'setText("Exportar log")',
        "REQUEST_EXPORT_AOT_CAPTURES",
        "REQUEST_EXPORT_LOG",
        "ACTION_CREATE_DOCUMENT",
        "ACTION_GET_CONTENT",
    ):
        require(
            forbidden not in activity,
            f"User-facing private game-data export control must stay removed: {forbidden}",
        )

    game_builder = (project_root / "tools/build_android_game.py").read_text(encoding="utf-8")
    for required in (
        "captures_zip = pull_private_aot_captures(",
        "except NoDeviceCaptures as error:",
        "continuing with the first strict capture bootstrap",
        "Captures remain in the app's private files directory",
    ):
        require(required in game_builder, f"Automatic private AOT synchronization is missing: {required}")

    capture_sync_path = project_root / "tools/sync_android_aot_captures.py"
    require(capture_sync_path.is_file(), "Private ADB capture synchronizer is missing.")
    capture_sync = capture_sync_path.read_text(encoding="utf-8")
    for required in (
        "run-as {package_name}",
        '"exec-out"',
        "capture_tar_to_zip(",
        "MAX_ARCHIVE_BYTES",
        "PurePosixPath",
    ):
        require(required in capture_sync, f"Private ADB capture synchronization is missing: {required}")

    native_aot_builder = project_root / "tools/build_android_native_aot.py"
    require(native_aot_builder.is_file(), "Private native AOT builder is missing.")
    native_aot_source = native_aot_builder.read_text(encoding="utf-8")
    for required in (
        "VU1_MICRO_MEMORY_BYTES",
        "normalize_and_validate_iop_dump",
        "--strict-partial",
        "vu1n_heads_combined.txt",
        "vu1/vu1n_heads.txt",
        "full_native_aot",
    ):
        require(required in native_aot_source, f"Private native AOT builder is missing: {required}")

    game_builder = (project_root / "tools/build_android_game.py").read_text(encoding="utf-8")
    for required in (
        '"--iso"',
        '"--generate-only"',
        '"--plan-only"',
        "--aot-captures",
        "prepare_runtime_command",
        "pull_private_aot_captures(",
        "GOW_ANDROID_CAPTURE_BOOTSTRAP",
    ):
        require(required in game_builder, f"Android ISO-only AOT build orchestration is missing: {required}")
    require(
        "--capture-bootstrap" not in game_builder,
        "First-boot AOT bootstrap must be automatic, not a user-selectable export/build mode.",
    )

    print("Verified: Android PS2 core initialization is headless and uses a null GS backend.")
    print("Verified: the Android guest worker starts only after private AOT, GS backend and Vulkan surface gates pass.")
    print("Verified: GameActivity owns presentation and bounded local GS scene captures.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"Android runtime lifecycle check failed: {error}", file=sys.stderr)
        raise SystemExit(1)
