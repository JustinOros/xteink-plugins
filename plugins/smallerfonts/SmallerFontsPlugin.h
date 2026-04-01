#pragma once

#include <Arduino.h>

enum class SmallerFontsMode : uint8_t {
    MODE_OFF  = 0,
    SMALLER   = 1,
    SMALLEST  = 2,
};

namespace SmallerFontsPlugin {
    const char* modeName(SmallerFontsMode mode);
    int         resolveReaderFontId(int baseFontId, SmallerFontsMode mode);
}
