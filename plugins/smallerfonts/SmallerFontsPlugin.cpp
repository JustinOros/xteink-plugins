#include "SmallerFontsPlugin.h"
#include "fontIds.h"

namespace SmallerFontsPlugin {

const char* modeName(SmallerFontsMode mode) {
    switch (mode) {
        case SmallerFontsMode::ENABLED: return "Enabled";
        default:                        return "Disabled";
    }
}

int resolveReaderFontId(int baseFontId, SmallerFontsMode mode) {
    if (mode == SmallerFontsMode::MODE_OFF) return baseFontId;

    if (baseFontId == NOTOSERIF_12_FONT_ID) return NOTOSERIF_12_FONT_ID;
    if (baseFontId == NOTOSERIF_14_FONT_ID) return NOTOSERIF_12_FONT_ID;
    if (baseFontId == NOTOSERIF_16_FONT_ID) return NOTOSERIF_14_FONT_ID;
    if (baseFontId == NOTOSERIF_18_FONT_ID) return NOTOSERIF_16_FONT_ID;

    if (baseFontId == NOTOSANS_12_FONT_ID) return NOTOSANS_12_FONT_ID;
    if (baseFontId == NOTOSANS_14_FONT_ID) return NOTOSANS_12_FONT_ID;
    if (baseFontId == NOTOSANS_16_FONT_ID) return NOTOSANS_14_FONT_ID;
    if (baseFontId == NOTOSANS_18_FONT_ID) return NOTOSANS_16_FONT_ID;

    if (baseFontId == BOOKERLY_12_FONT_ID) return BOOKERLY_12_FONT_ID;
    if (baseFontId == BOOKERLY_14_FONT_ID) return BOOKERLY_12_FONT_ID;
    if (baseFontId == BOOKERLY_16_FONT_ID) return BOOKERLY_14_FONT_ID;
    if (baseFontId == BOOKERLY_18_FONT_ID) return BOOKERLY_16_FONT_ID;

    return baseFontId;
}

}
