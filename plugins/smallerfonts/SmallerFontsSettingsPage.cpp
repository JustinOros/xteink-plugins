#include "SmallerFontsSettingsActivity.h"
#include "fontIds.h"

void SmallerFontsSettingsActivity::render(GfxRenderer& renderer, SmallerFontsMode currentMode) {
    const int LABEL_X    = 10;
    const int VALUE_X    = 160;
    const int ROW_Y      = 60;
    renderer.drawText(UI_12_FONT_ID, LABEL_X, ROW_Y, "Smaller Fonts");
    renderer.drawText(UI_12_FONT_ID, VALUE_X, ROW_Y, SmallerFontsPlugin::modeName(currentMode));
}

SmallerFontsMode SmallerFontsSettingsActivity::handleNext(SmallerFontsMode currentMode) {
    uint8_t v = static_cast<uint8_t>(currentMode);
    if (v < 3) v++;
    return static_cast<SmallerFontsMode>(v);
}

SmallerFontsMode SmallerFontsSettingsActivity::handlePrev(SmallerFontsMode currentMode) {
    uint8_t v = static_cast<uint8_t>(currentMode);
    if (v > 0) v--;
    return static_cast<SmallerFontsMode>(v);
}
