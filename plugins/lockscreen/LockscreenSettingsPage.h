#pragma once

#include <GfxRenderer.h>
#include "LockscreenPlugin.h"

class LockscreenSettingsActivity {
public:
    static void render(GfxRenderer& renderer, LockscreenMode currentMode);
    static LockscreenMode handleNext(LockscreenMode currentMode);
    static LockscreenMode handlePrev(LockscreenMode currentMode);
};
