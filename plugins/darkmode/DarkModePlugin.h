#pragma once

#include <Arduino.h>

enum class DarkModeState : uint8_t {
    MODE_OFF = 0,
    MODE_ON  = 1,
};

namespace DarkModePlugin {
    const char* stateName(DarkModeState state);
    bool        isDarkMode(DarkModeState state);
}
