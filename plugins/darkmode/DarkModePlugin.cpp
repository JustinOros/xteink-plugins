#include "DarkModePlugin.h"

namespace DarkModePlugin {

const char* stateName(DarkModeState state) {
    switch (state) {
        case DarkModeState::MODE_ON: return "Enabled";
        default:                     return "Disabled";
    }
}

bool isDarkMode(DarkModeState state) {
    return state == DarkModeState::MODE_ON;
}

}
