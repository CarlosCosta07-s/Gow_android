#include "android_performance_hint.h"

#if defined(__ANDROID__)
#include <android/log.h>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstdlib>

namespace gow::android {
namespace {

constexpr char kTag[] = "GowAndroid";
constexpr int64_t kMaximumReportedWorkNs = 1'000'000'000ll;

struct APerformanceHintManager;
struct APerformanceHintSession;

using GetManagerFn = APerformanceHintManager *(*)();
using CreateSessionFn = APerformanceHintSession *(
    *)(APerformanceHintManager *, const int32_t *, size_t, int64_t);
using ReportActualFn = int (*)(APerformanceHintSession *, int64_t);
using UpdateTargetFn = int (*)(APerformanceHintSession *, int64_t);

struct AdpfState {
    void *library = nullptr;
    APerformanceHintSession *session = nullptr;
    ReportActualFn reportActual = nullptr;
    UpdateTargetFn updateTarget = nullptr;
    int64_t targetNs = 0;
    int state = 0; // 0 = not initialized, 1 = active, -1 = unavailable/disabled
};

AdpfState gAdpf;

void initializeAdpf(int64_t targetNs) noexcept {
    gAdpf.state = -1;

    const char *disabled = std::getenv("GOW_ANDROID_ADPF");
    if (disabled != nullptr && disabled[0] == '0') {
        __android_log_print(
            ANDROID_LOG_INFO, kTag,
            "ADPF disabled by GOW_ANDROID_ADPF=0");
        return;
    }

    gAdpf.library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (gAdpf.library == nullptr) {
        __android_log_print(
            ANDROID_LOG_INFO, kTag,
            "ADPF unavailable: libandroid.so could not be opened");
        return;
    }

    const auto getManager = reinterpret_cast<GetManagerFn>(
        dlsym(gAdpf.library, "APerformanceHint_getManager"));
    const auto createSession = reinterpret_cast<CreateSessionFn>(
        dlsym(gAdpf.library, "APerformanceHint_createSession"));
    gAdpf.reportActual = reinterpret_cast<ReportActualFn>(
        dlsym(gAdpf.library, "APerformanceHint_reportActualWorkDuration"));
    gAdpf.updateTarget = reinterpret_cast<UpdateTargetFn>(
        dlsym(gAdpf.library, "APerformanceHint_updateTargetWorkDuration"));

    if (getManager == nullptr || createSession == nullptr ||
        gAdpf.reportActual == nullptr || gAdpf.updateTarget == nullptr) {
        __android_log_print(
            ANDROID_LOG_INFO, kTag,
            "ADPF unavailable: this Android version does not export the required APIs");
        return;
    }

    APerformanceHintManager *manager = getManager();
    const int32_t threadId = static_cast<int32_t>(syscall(SYS_gettid));
    if (manager == nullptr || threadId <= 0 || targetNs <= 0) {
        __android_log_print(
            ANDROID_LOG_WARN, kTag,
            "ADPF unavailable: manager, thread id or frame budget is invalid");
        return;
    }

    gAdpf.session = createSession(manager, &threadId, 1u, targetNs);
    if (gAdpf.session == nullptr) {
        __android_log_print(
            ANDROID_LOG_INFO, kTag,
            "ADPF unavailable: system did not create a performance-hint session");
        return;
    }

    gAdpf.targetNs = targetNs;
    gAdpf.state = 1;
    __android_log_print(
        ANDROID_LOG_INFO, kTag,
        "ADPF active for Android host thread %d (target %.2f ms)",
        threadId, static_cast<double>(targetNs) / 1'000'000.0);
}

} // namespace

void reportPerformanceHintFrame(
    uint64_t actualWorkDurationNs,
    uint64_t targetWorkDurationNs) noexcept {
    if (actualWorkDurationNs == 0u || targetWorkDurationNs == 0u) {
        return;
    }

    if (gAdpf.state == 0) {
        initializeAdpf(static_cast<int64_t>(targetWorkDurationNs));
    }
    if (gAdpf.state != 1 || gAdpf.session == nullptr) {
        return;
    }

    const int64_t boundedTarget = static_cast<int64_t>(
        targetWorkDurationNs > static_cast<uint64_t>(INT64_MAX)
            ? static_cast<uint64_t>(INT64_MAX)
            : targetWorkDurationNs);
    if (boundedTarget != gAdpf.targetNs) {
        if (gAdpf.updateTarget(gAdpf.session, boundedTarget) != 0) {
            __android_log_print(
                ANDROID_LOG_WARN, kTag,
                "ADPF disabled: failed to update target work duration");
            gAdpf.state = -1;
            return;
        }
        gAdpf.targetNs = boundedTarget;
    }

    const uint64_t boundedWork = actualWorkDurationNs >
            static_cast<uint64_t>(kMaximumReportedWorkNs)
        ? static_cast<uint64_t>(kMaximumReportedWorkNs)
        : actualWorkDurationNs;
    if (gAdpf.reportActual(gAdpf.session, static_cast<int64_t>(boundedWork)) != 0) {
        __android_log_print(
            ANDROID_LOG_WARN, kTag,
            "ADPF disabled: failed to report actual frame work");
        gAdpf.state = -1;
    }
}

} // namespace gow::android

#else

namespace gow::android {

void reportPerformanceHintFrame(
    uint64_t actualWorkDurationNs,
    uint64_t targetWorkDurationNs) noexcept {
    (void)actualWorkDurationNs;
    (void)targetWorkDurationNs;
}

} // namespace gow::android

#endif

namespace gow::android {

uint64_t performanceHintTargetDurationNs(uint64_t vsyncTickDelta) noexcept {
    constexpr uint64_t kMaximumBlanks = 4u;
    const uint64_t blanks = vsyncTickDelta == 0u
        ? 1u
        : (vsyncTickDelta > kMaximumBlanks ? kMaximumBlanks : vsyncTickDelta);
    return blanks * kPs2NtscVblankBudgetNs;
}

} // namespace gow::android
