#include "dualshock2_packet.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {
int fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}
}

int main() {
    using namespace gow::android_input;
    std::array<uint8_t, 32> packet{};

    DualShock2State neutral{};
    DualShock2Config defaultConfig{};
    if (!encodeDualShock2Packet(0, 0, neutral, defaultConfig,
                                packet.data(), packet.size())) {
        return fail("neutral packet rejected");
    }
    if (packet[0] != 0x01u || packet[1] != 0x73u ||
        packet[2] != 0xFFu || packet[3] != 0xFFu) {
        return fail("neutral header/button state is not active-low analog mode");
    }
    for (size_t i = 4; i < 8; ++i) {
        if (packet[i] != 128u) {
            return fail("neutral stick axis is not centered");
        }
    }

    // CROSS + UP + R2. Waffle's pad layout stores buttons active-low.
    DualShock2State buttons{};
    buttons.pressedButtons = 0x4000u | 0x0010u | 0x0200u;
    if (!encodeDualShock2Packet(0, 0, buttons, defaultConfig,
                                packet.data(), packet.size())) {
        return fail("button packet rejected");
    }
    if (packet[2] != 0xEFu || packet[3] != 0xBDu) {
        return fail("button bits or active-low byte order mismatch");
    }

    DualShock2Config linear{};
    linear.deadzone = 0.0f;
    linear.sensitivity = 1.0f;
    DualShock2State axes{};
    axes.leftX = -1.0f;
    axes.leftY = 1.0f;
    axes.rightX = 0.5f;
    axes.rightY = -0.5f;
    encodeDualShock2Packet(0, 0, axes, linear, packet.data(), packet.size());
    if (packet[4] != 192u || packet[5] != 64u ||
        packet[6] != 1u || packet[7] != 255u) {
        return fail("right/left analog axis order or conversion mismatch");
    }

    DualShock2Config deadzone{};
    deadzone.deadzone = 0.2f;
    deadzone.sensitivity = 1.0f;
    axes = {};
    axes.leftX = 0.1f;
    axes.rightX = 0.6f;
    encodeDualShock2Packet(0, 0, axes, deadzone, packet.data(), packet.size());
    if (packet[6] != 128u || packet[4] != 192u) {
        return fail("deadzone remapping mismatch");
    }

    DualShock2Config inverted = linear;
    inverted.invertLeftX = true;
    axes = {};
    axes.leftX = 0.5f;
    encodeDualShock2Packet(0, 0, axes, inverted, packet.data(), packet.size());
    if (packet[6] != 64u) {
        return fail("axis inversion mismatch");
    }

    if (encodeDualShock2Packet(1, 0, neutral, defaultConfig,
                               packet.data(), packet.size())) {
        return fail("unsupported port unexpectedly accepted");
    }
    if (encodeDualShock2Packet(0, 0, neutral, defaultConfig,
                               packet.data(), 31u)) {
        return fail("short packet buffer unexpectedly accepted");
    }

    std::cout << "DualShock 2 packet tests passed\n";
    return 0;
}
