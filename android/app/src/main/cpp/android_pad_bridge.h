#pragma once

#include <android/input.h>
#include <cstddef>
#include <cstdint>

extern "C" {
int32_t gowAndroidPadHandleInputEvent(AInputEvent *event) noexcept;
void gowAndroidPadResetAll() noexcept;
void gowAndroidPadSetTouchButtons(uint16_t pressedMask) noexcept;
void gowAndroidPadSetTouchAxes(float leftX, float leftY, float rightX, float rightY) noexcept;
bool gowAndroidPadReadState(int port, int slot, uint8_t *data, size_t size) noexcept;
void gowAndroidPadConfigure(float deadzone, float sensitivity,
                            bool invertLeftX, bool invertLeftY,
                            bool invertRightX, bool invertRightY) noexcept;
}
