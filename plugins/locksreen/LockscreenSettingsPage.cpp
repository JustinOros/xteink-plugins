#include "LockscreenSettingsPage.h"
#include "fontIds.h"

void LockscreenSettingsActivity::render(GfxRenderer& renderer, LockscreenMode currentMode) {
    const int LABEL_X = 10;
    const int VALUE_X = 160;
    const int ROW_Y   = 60;
    renderer.drawText(UI_12_FONT_ID, LABEL_X, ROW_Y, "Lockscreen");
    renderer.drawText(UI_12_FONT_ID, VALUE_X, ROW_Y, LockscreenPlugin::modeName(currentMode));
}

LockscreenMode LockscreenSettingsActivity::handleNext(LockscreenMode currentMode) {
    uint8_t v = static_cast<uint8_t>(currentMode);
    if (v < 1) v++;
    return static_cast<LockscreenMode>(v);
}

LockscreenMode LockscreenSettingsActivity::handlePrev(LockscreenMode currentMode) {
    uint8_t v = static_cast<uint8_t>(currentMode);
    if (v > 0) v--;
    return static_cast<LockscreenMode>(v);
}
