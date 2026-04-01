#pragma once

#include <GfxRenderer.h>
#include "SmallerFontsPlugin.h"

class SmallerFontsSettingsActivity {
public:
    static void render(GfxRenderer& renderer, SmallerFontsMode currentMode);
    static SmallerFontsMode handleNext(SmallerFontsMode currentMode);
    static SmallerFontsMode handlePrev(SmallerFontsMode currentMode);
};
