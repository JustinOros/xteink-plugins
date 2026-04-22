#include "SmallerFontsPlugin.h"
#include "fontIds.h"

namespace SmallerFontsPlugin {

const char* modeName(SmallerFontsMode mode) {
    switch (mode) {
        case SmallerFontsMode::SMALLER:  return "Smaller";
        case SmallerFontsMode::SMALLEST: return "Smallest";
        default:                         return "Disabled";
    }
}

int resolveReaderFontId(int baseFontId, SmallerFontsMode mode) {
    if (mode == SmallerFontsMode::MODE_OFF) return baseFontId;

#ifdef BOOKERLY_8_FONT_ID
    if (baseFontId == BOOKERLY_8_FONT_ID)  return BOOKERLY_8_FONT_ID;
    if (baseFontId == BOOKERLY_10_FONT_ID) {
        if (mode == SmallerFontsMode::SMALLER)  return BOOKERLY_8_FONT_ID;
        if (mode == SmallerFontsMode::SMALLEST) return BOOKERLY_8_FONT_ID;
    }
    if (baseFontId == BOOKERLY_12_FONT_ID) {
        if (mode == SmallerFontsMode::SMALLER)  return BOOKERLY_10_FONT_ID;
        if (mode == SmallerFontsMode::SMALLEST) return BOOKERLY_8_FONT_ID;
    }
    if (baseFontId == BOOKERLY_14_FONT_ID) {
        if (mode == SmallerFontsMode::SMALLER)  return BOOKERLY_12_FONT_ID;
        if (mode == SmallerFontsMode::SMALLEST) return BOOKERLY_10_FONT_ID;
    }
    if (baseFontId == BOOKERLY_16_FONT_ID) {
        if (mode == SmallerFontsMode::SMALLER)  return BOOKERLY_14_FONT_ID;
        if (mode == SmallerFontsMode::SMALLEST) return BOOKERLY_12_FONT_ID;
    }
    if (baseFontId == BOOKERLY_18_FONT_ID) {
        if (mode == SmallerFontsMode::SMALLER)  return BOOKERLY_16_FONT_ID;
        if (mode == SmallerFontsMode::SMALLEST) return BOOKERLY_14_FONT_ID;
    }
#endif

    if (baseFontId == NOTOSERIF_12_FONT_ID) return NOTOSERIF_12_FONT_ID;
    if (baseFontId == NOTOSERIF_14_FONT_ID) {
        if (mode == SmallerFontsMode::SMALLER)  return NOTOSERIF_12_FONT_ID;
        if (mode == SmallerFontsMode::SMALLEST) return NOTOSERIF_12_FONT_ID;
    }
    if (baseFontId == NOTOSERIF_16_FONT_ID) {
        if (mode == SmallerFontsMode::SMALLER)  return NOTOSERIF_14_FONT_ID;
        if (mode == SmallerFontsMode::SMALLEST) return NOTOSERIF_12_FONT_ID;
    }
    if (baseFontId == NOTOSERIF_18_FONT_ID) {
        if (mode == SmallerFontsMode::SMALLER)  return NOTOSERIF_16_FONT_ID;
        if (mode == SmallerFontsMode::SMALLEST) return NOTOSERIF_14_FONT_ID;
    }

    if (baseFontId == NOTOSANS_12_FONT_ID) return NOTOSANS_12_FONT_ID;
    if (baseFontId == NOTOSANS_14_FONT_ID) {
        if (mode == SmallerFontsMode::SMALLER)  return NOTOSANS_12_FONT_ID;
        if (mode == SmallerFontsMode::SMALLEST) return NOTOSANS_12_FONT_ID;
    }
    if (baseFontId == NOTOSANS_16_FONT_ID) {
        if (mode == SmallerFontsMode::SMALLER)  return NOTOSANS_14_FONT_ID;
        if (mode == SmallerFontsMode::SMALLEST) return NOTOSANS_12_FONT_ID;
    }
    if (baseFontId == NOTOSANS_18_FONT_ID) {
        if (mode == SmallerFontsMode::SMALLER)  return NOTOSANS_16_FONT_ID;
        if (mode == SmallerFontsMode::SMALLEST) return NOTOSANS_14_FONT_ID;
    }

    if (baseFontId == OPENDYSLEXIC_8_FONT_ID)  return OPENDYSLEXIC_8_FONT_ID;
    if (baseFontId == OPENDYSLEXIC_10_FONT_ID) {
        if (mode == SmallerFontsMode::SMALLER)  return OPENDYSLEXIC_8_FONT_ID;
        if (mode == SmallerFontsMode::SMALLEST) return OPENDYSLEXIC_8_FONT_ID;
    }
    if (baseFontId == OPENDYSLEXIC_12_FONT_ID) {
        if (mode == SmallerFontsMode::SMALLER)  return OPENDYSLEXIC_10_FONT_ID;
        if (mode == SmallerFontsMode::SMALLEST) return OPENDYSLEXIC_8_FONT_ID;
    }
    if (baseFontId == OPENDYSLEXIC_14_FONT_ID) {
        if (mode == SmallerFontsMode::SMALLER)  return OPENDYSLEXIC_12_FONT_ID;
        if (mode == SmallerFontsMode::SMALLEST) return OPENDYSLEXIC_10_FONT_ID;
    }

    return baseFontId;
}

}
