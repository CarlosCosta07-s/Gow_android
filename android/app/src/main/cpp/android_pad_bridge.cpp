#include "android_pad_bridge.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include <android/keycodes.h>

namespace {
constexpr uint16_t kLeft = 0x0080u, kDown = 0x0040u, kRight = 0x0020u, kUp = 0x0010u;
constexpr uint16_t kStart = 0x0008u, kR3 = 0x0004u, kL3 = 0x0002u, kSelect = 0x0001u;
constexpr uint16_t kSquare = 0x8000u, kCross = 0x4000u, kCircle = 0x2000u, kTriangle = 0x1000u;
constexpr uint16_t kR1 = 0x0800u, kL1 = 0x0400u, kR2 = 0x0200u, kL2 = 0x0100u;

std::atomic<uint16_t> g_physicalButtons{0u};
std::atomic<uint16_t> g_touchButtons{0u};
std::atomic<float> g_leftX{0.0f}, g_leftY{0.0f}, g_rightX{0.0f}, g_rightY{0.0f};
std::atomic<float> g_touchLeftX{0.0f}, g_touchLeftY{0.0f}, g_touchRightX{0.0f}, g_touchRightY{0.0f};
std::atomic<float> g_deadzone{0.15f}, g_sensitivity{1.0f};
std::atomic<bool> g_invertLeftX{false}, g_invertLeftY{false}, g_invertRightX{false}, g_invertRightY{false};

void setPhysicalButton(uint16_t mask, bool down) noexcept {
    if (down) g_physicalButtons.fetch_or(mask, std::memory_order_relaxed);
    else g_physicalButtons.fetch_and(static_cast<uint16_t>(~mask), std::memory_order_relaxed);
}

bool mapKeyCode(int32_t keyCode, uint16_t &mask) noexcept {
    switch (keyCode) {
        case AKEYCODE_DPAD_LEFT: mask = kLeft; return true;
        case AKEYCODE_DPAD_DOWN: mask = kDown; return true;
        case AKEYCODE_DPAD_RIGHT: mask = kRight; return true;
        case AKEYCODE_DPAD_UP: mask = kUp; return true;
        case AKEYCODE_BUTTON_START: mask = kStart; return true;
        case AKEYCODE_BUTTON_SELECT: mask = kSelect; return true;
        case AKEYCODE_BUTTON_THUMBL: mask = kL3; return true;
        case AKEYCODE_BUTTON_THUMBR: mask = kR3; return true;
        case AKEYCODE_BUTTON_A: mask = kCross; return true;
        case AKEYCODE_BUTTON_B: mask = kCircle; return true;
        case AKEYCODE_BUTTON_X: mask = kSquare; return true;
        case AKEYCODE_BUTTON_Y: mask = kTriangle; return true;
        case AKEYCODE_BUTTON_L1: mask = kL1; return true;
        case AKEYCODE_BUTTON_R1: mask = kR1; return true;
        case AKEYCODE_BUTTON_L2: mask = kL2; return true;
        case AKEYCODE_BUTTON_R2: mask = kR2; return true;
        default: return false;
    }
}

float readAxis(const AInputEvent *event, int32_t axis) noexcept {
    return std::clamp(AMotionEvent_getAxisValue(event, axis, 0), -1.0f, 1.0f);
}

float applyAxisConfig(float value, bool invert) noexcept {
    const float dz = std::clamp(g_deadzone.load(std::memory_order_relaxed), 0.0f, 0.8f);
    const float gain = std::clamp(g_sensitivity.load(std::memory_order_relaxed), 0.1f, 2.0f);
    value = std::clamp(value, -1.0f, 1.0f);
    const float magnitude = std::fabs(value);
    if (magnitude <= dz) value = 0.0f;
    else value = std::copysign((magnitude - dz) / (1.0f - dz), value);
    value = std::clamp(value * gain, -1.0f, 1.0f);
    return invert ? -value : value;
}

uint8_t axisToByte(float value, bool invert) noexcept {
    const int converted = static_cast<int>(std::lround(128.0f + applyAxisConfig(value, invert) * 127.0f));
    return static_cast<uint8_t>(std::clamp(converted, 1, 255));
}
} // namespace

extern "C" int32_t gowAndroidPadHandleInputEvent(AInputEvent *event) noexcept {
    if (!event) return 0;
    const int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_KEY) {
        uint16_t mask = 0u;
        if (!mapKeyCode(AKeyEvent_getKeyCode(event), mask)) return 0;
        const int32_t action = AKeyEvent_getAction(event);
        if (action == AKEY_EVENT_ACTION_DOWN) setPhysicalButton(mask, true);
        else if (action == AKEY_EVENT_ACTION_UP) setPhysicalButton(mask, false);
        return 1;
    }
    if (type != AINPUT_EVENT_TYPE_MOTION) return 0;

    const int32_t source = AInputEvent_getSource(event);
    if ((source & AINPUT_SOURCE_JOYSTICK) != AINPUT_SOURCE_JOYSTICK &&
        (source & AINPUT_SOURCE_GAMEPAD) != AINPUT_SOURCE_GAMEPAD) return 0;
    if ((AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK) != AMOTION_EVENT_ACTION_MOVE) return 0;

    const float lx = readAxis(event, AMOTION_EVENT_AXIS_X);
    const float ly = readAxis(event, AMOTION_EVENT_AXIS_Y);
    float rx = readAxis(event, AMOTION_EVENT_AXIS_Z);
    float ry = readAxis(event, AMOTION_EVENT_AXIS_RZ);
    if (std::fabs(rx) < 0.001f) rx = readAxis(event, AMOTION_EVENT_AXIS_RX);
    if (std::fabs(ry) < 0.001f) ry = readAxis(event, AMOTION_EVENT_AXIS_RY);
    g_leftX.store(lx, std::memory_order_relaxed);
    g_leftY.store(ly, std::memory_order_relaxed);
    g_rightX.store(rx, std::memory_order_relaxed);
    g_rightY.store(ry, std::memory_order_relaxed);

    float lt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0);
    float rt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0);
    if (lt <= 0.001f) lt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_BRAKE, 0);
    if (rt <= 0.001f) rt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_GAS, 0);
    setPhysicalButton(kL2, lt > 0.5f);
    setPhysicalButton(kR2, rt > 0.5f);
    return 1;
}

extern "C" void gowAndroidPadResetAll() noexcept {
    g_physicalButtons.store(0u, std::memory_order_relaxed);
    g_touchButtons.store(0u, std::memory_order_relaxed);
    g_leftX.store(0.0f, std::memory_order_relaxed); g_leftY.store(0.0f, std::memory_order_relaxed);
    g_rightX.store(0.0f, std::memory_order_relaxed); g_rightY.store(0.0f, std::memory_order_relaxed);
    g_touchLeftX.store(0.0f, std::memory_order_relaxed); g_touchLeftY.store(0.0f, std::memory_order_relaxed);
    g_touchRightX.store(0.0f, std::memory_order_relaxed); g_touchRightY.store(0.0f, std::memory_order_relaxed);
}

extern "C" void gowAndroidPadSetTouchButtons(uint16_t mask) noexcept {
    g_touchButtons.store(mask, std::memory_order_relaxed);
}

extern "C" void gowAndroidPadSetTouchAxes(float lx, float ly, float rx, float ry) noexcept {
    g_touchLeftX.store(std::clamp(lx, -1.0f, 1.0f), std::memory_order_relaxed);
    g_touchLeftY.store(std::clamp(ly, -1.0f, 1.0f), std::memory_order_relaxed);
    g_touchRightX.store(std::clamp(rx, -1.0f, 1.0f), std::memory_order_relaxed);
    g_touchRightY.store(std::clamp(ry, -1.0f, 1.0f), std::memory_order_relaxed);
}

extern "C" void gowAndroidPadConfigure(float dz, float gain, bool ilx, bool ily, bool irx, bool iry) noexcept {
    g_deadzone.store(std::clamp(dz, 0.0f, 0.8f), std::memory_order_relaxed);
    g_sensitivity.store(std::clamp(gain, 0.1f, 2.0f), std::memory_order_relaxed);
    g_invertLeftX.store(ilx, std::memory_order_relaxed);
    g_invertLeftY.store(ily, std::memory_order_relaxed);
    g_invertRightX.store(irx, std::memory_order_relaxed);
    g_invertRightY.store(iry, std::memory_order_relaxed);
}

extern "C" bool gowAndroidPadReadState(int port, int slot, uint8_t *data, size_t size) noexcept {
    if (!data || size < 32u || port != 0 || slot != 0) return false;
    std::memset(data, 0, size);
    const uint16_t pressed = static_cast<uint16_t>(
        g_physicalButtons.load(std::memory_order_relaxed) |
        g_touchButtons.load(std::memory_order_relaxed));
    const uint16_t activeLow = static_cast<uint16_t>(~pressed);
    data[0] = 0x01u;
    data[1] = 0x73u;
    data[2] = static_cast<uint8_t>(activeLow & 0xFFu);
    data[3] = static_cast<uint8_t>((activeLow >> 8u) & 0xFFu);
    const float lx = std::clamp(g_leftX.load(std::memory_order_relaxed) + g_touchLeftX.load(std::memory_order_relaxed), -1.0f, 1.0f);
    const float ly = std::clamp(g_leftY.load(std::memory_order_relaxed) + g_touchLeftY.load(std::memory_order_relaxed), -1.0f, 1.0f);
    const float rx = std::clamp(g_rightX.load(std::memory_order_relaxed) + g_touchRightX.load(std::memory_order_relaxed), -1.0f, 1.0f);
    const float ry = std::clamp(g_rightY.load(std::memory_order_relaxed) + g_touchRightY.load(std::memory_order_relaxed), -1.0f, 1.0f);
    data[4] = axisToByte(rx, g_invertRightX.load(std::memory_order_relaxed));
    data[5] = axisToByte(ry, g_invertRightY.load(std::memory_order_relaxed));
    data[6] = axisToByte(lx, g_invertLeftX.load(std::memory_order_relaxed));
    data[7] = axisToByte(ly, g_invertLeftY.load(std::memory_order_relaxed));
    return true;
}
