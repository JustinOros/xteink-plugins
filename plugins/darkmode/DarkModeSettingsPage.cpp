#include "DarkModeSettingsActivity.h"
#include "fontIds.h"

void DarkModeSettingsActivity::render(GfxRenderer& renderer, DarkModeState currentState) {
    const int LABEL_X = 10;
    const int VALUE_X = 160;
    const int ROW_Y   = 60;
    renderer.drawText(UI_12_FONT_ID, LABEL_X, ROW_Y, "Dark Mode");
    renderer.drawText(UI_12_FONT_ID, VALUE_X, ROW_Y, DarkModePlugin::stateName(currentState));
}

DarkModeState DarkModeSettingsActivity::handleNext(DarkModeState currentState) {
    uint8_t v = static_cast<uint8_t>(currentState);
    if (v < 1) v++;
    return static_cast<DarkModeState>(v);
}

DarkModeState DarkModeSettingsActivity::handlePrev(DarkModeState currentState) {
    uint8_t v = static_cast<uint8_t>(currentState);
    if (v > 0) v--;
    return static_cast<DarkModeState>(v);
}
