#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gow::android_input {

struct DualShock2State {
    uint16_t pressedButtons = 0u;
    float leftX = 0.0f;
    float leftY = 0.0f;
    float rightX = 0.0f;
    float rightY = 0.0f;
};

struct DualShock2Config {
    float deadzone = 0.15f;
    float sensitivity = 1.0f;
    bool invertLeftX = false;
    bool invertLeftY = false;
    bool invertRightX = false;
    bool invertRightY = false;
};

inline float applyAxisConfig(float value, float deadzone, float sensitivity,
                             bool invert) noexcept {
    const float dz = std::clamp(deadzone, 0.0f, 0.8f);
    const float gain = std::clamp(sensitivity, 0.1f, 2.0f);
    value = std::clamp(value, -1.0f, 1.0f);
    const float magnitude = std::fabs(value);
    if (magnitude <= dz) {
        value = 0.0f;
    } else {
        value = std::copysign((magnitude - dz) / (1.0f - dz), value);
    }
    value = std::clamp(value * gain, -1.0f, 1.0f);
    return invert ? -value : value;
}

inline uint8_t axisToByte(float value, const DualShock2Config &config,
                          bool invert) noexcept {
    const float adjusted = applyAxisConfig(
        value, config.deadzone, config.sensitivity, invert);
    const int converted = static_cast<int>(
        std::lround(128.0f + adjusted * 127.0f));
    return static_cast<uint8_t>(std::clamp(converted, 1, 255));
}

// DualShock 2 analog-mode packet used by the Waffle PSPadBackend:
// bytes 2/3 are active-low button bits; right stick precedes left stick.
inline bool encodeDualShock2Packet(int port, int slot,
                                   const DualShock2State &state,
                                   const DualShock2Config &config,
                                   uint8_t *data, size_t size) noexcept {
    if (data == nullptr || size < 32u || port != 0 || slot != 0) {
        return false;
    }

    std::memset(data, 0, 32u);
    data[0] = 0x01u;
    data[1] = 0x73u;

    const uint16_t activeLow = static_cast<uint16_t>(~state.pressedButtons);
    data[2] = static_cast<uint8_t>(activeLow & 0xFFu);
    data[3] = static_cast<uint8_t>((activeLow >> 8u) & 0xFFu);
    data[4] = axisToByte(state.rightX, config, config.invertRightX);
    data[5] = axisToByte(state.rightY, config, config.invertRightY);
    data[6] = axisToByte(state.leftX, config, config.invertLeftX);
    data[7] = axisToByte(state.leftY, config, config.invertLeftY);
    return true;
}

} // namespace gow::android_input
