/*
 * Copyright (C) 2018 The Android Open Source Project
 * Copyright (C) 2020 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Author := dev_harsh1998, Isaac Chen

#define LOG_TAG "android.hardware.light@2.0-impl.wayne"
/* #define LOG_NDEBUG 0 */

#include "Light.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <unistd.h>

namespace {

#define PPCAT_NX(A, B) A/B
#define PPCAT(A, B) PPCAT_NX(A, B)
#define STRINGIFY_INNER(x) #x
#define STRINGIFY(x) STRINGIFY_INNER(x)

#define LEDS(x) PPCAT(/sys/class/leds, x)
#define LCD_ATTR(x) STRINGIFY(PPCAT(LEDS(lcd-backlight), x))
#define WHITE_ATTR(x) STRINGIFY(PPCAT(LEDS(red), x))

using ::android::base::ReadFileToString;
using ::android::base::WriteStringToFile;

// Default max brightness
constexpr auto kDefaultMaxLedBrightness = 255;
constexpr auto kDefaultMaxScreenBrightness = 4095;

// Write value to path and close file.
bool WriteToFile(const std::string& path, uint32_t content) {
    return WriteStringToFile(std::to_string(content), path);
}

uint32_t RgbaToBrightness(uint32_t color) {
    // Extract brightness from AARRGGBB.
    uint32_t alpha = (color >> 24) & 0xFF;

    // Retrieve each of the RGB colors
    uint32_t red = (color >> 16) & 0xFF;
    uint32_t green = (color >> 8) & 0xFF;
    uint32_t blue = color & 0xFF;

    // Scale RGB colors if a brightness has been applied by the user
    if (alpha != 0xFF) {
        red = red * alpha / 0xFF;
        green = green * alpha / 0xFF;
        blue = blue * alpha / 0xFF;
    }

    return (77 * red + 150 * green + 29 * blue) >> 8;
}

inline uint32_t RgbaToBrightness(uint32_t color, uint32_t max_brightness) {
    return RgbaToBrightness(color) * max_brightness / 0xFF;
}

inline bool IsLit(uint32_t color) {
    return color & 0x00ffffff;
}

}  // anonymous namespace

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

Light::Light() {
    std::string buf;

    if (ReadFileToString(LCD_ATTR(max_brightness), &buf)) {
        max_screen_brightness_ = std::stoi(buf);
    } else {
        max_screen_brightness_ = kDefaultMaxScreenBrightness;
        LOG(ERROR) << "Failed to read max screen brightness, fallback to "
                   << kDefaultMaxScreenBrightness;
    }

    if (ReadFileToString(WHITE_ATTR(max_brightness), &buf)) {
        max_led_brightness_ = std::stoi(buf);
    } else {
        max_led_brightness_ = kDefaultMaxLedBrightness;
        LOG(ERROR) << "Failed to read max LED brightness, fallback to " << kDefaultMaxLedBrightness;
    }
}

Return<Status> Light::setLight(Type type, const LightState& state) {
    auto it = lights_.find(type);

    if (it == lights_.end()) {
        return Status::LIGHT_NOT_SUPPORTED;
    }

    it->second(type, state);

    return Status::SUCCESS;
}

Return<void> Light::getSupportedTypes(getSupportedTypes_cb _hidl_cb) {
    std::vector<Type> types;

    for (auto&& light : lights_) types.emplace_back(light.first);

    _hidl_cb(types);

    return Void();
}

void Light::setLightBacklight(Type /*type*/, const LightState& state) {
    uint32_t brightness = RgbaToBrightness(state.color, max_screen_brightness_);
    WriteToFile(LCD_ATTR(brightness), brightness);
}

void Light::setLightNotification(Type type, const LightState& state) {
    bool found = false;
    for (auto&& [cur_type, cur_state] : notif_states_) {
        if (cur_type == type) {
            cur_state = state;
        }

        // Fallback to battery light
        if (!found && (cur_type == Type::BATTERY || IsLit(cur_state.color))) {
            found = true;
            LOG(DEBUG) << __func__ << ": type=" << toString(cur_type);
            applyNotificationState(cur_state);
        }
    }
}

void Light::applyNotificationState(const LightState& state) {
    uint32_t white_brightness = RgbaToBrightness(state.color, max_led_brightness_);

    // Turn off the leds (initially)
    WriteToFile(WHITE_ATTR(breath), 0);

    if (state.flashMode == Flash::TIMED && state.flashOnMs > 0 && state.flashOffMs > 0) {
        LOG(DEBUG) << __func__ << ": color=" << std::hex << state.color << std::dec
                   << " onMs=" << state.flashOnMs << " offMs=" << state.flashOffMs;

        // White
        WriteToFile(WHITE_ATTR(delay_off), static_cast<uint32_t>(state.flashOffMs));
        WriteToFile(WHITE_ATTR(delay_on), static_cast<uint32_t>(state.flashOnMs));
        WriteToFile(WHITE_ATTR(breath), 1);
    } else {
        WriteToFile(WHITE_ATTR(brightness), white_brightness);
    }
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
