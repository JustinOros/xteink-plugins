#pragma once

#include <Arduino.h>

enum class BookerlyStyle : uint8_t {
    REGULAR     = 0,
    ITALIC      = 1,
    BOLD        = 2,
    BOLD_ITALIC = 3,
};

namespace BookerlyPlugin {
    const char* styleName(BookerlyStyle style);
}
