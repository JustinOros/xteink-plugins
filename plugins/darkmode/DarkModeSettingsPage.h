#pragma once

#include <GfxRenderer.h>
#include "DarkModePlugin.h"

class DarkModeSettingsActivity {
public:
    static void render(GfxRenderer& renderer, DarkModeState currentState);
    static DarkModeState handleNext(DarkModeState currentState);
    static DarkModeState handlePrev(DarkModeState currentState);
};
