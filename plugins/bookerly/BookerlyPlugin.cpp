#include "BookerlyPlugin.h"

namespace BookerlyPlugin {

const char* styleName(BookerlyStyle style) {
    switch (style) {
        case BookerlyStyle::ITALIC:      return "Italic";
        case BookerlyStyle::BOLD:        return "Bold";
        case BookerlyStyle::BOLD_ITALIC: return "Bold Italic";
        default:                         return "Regular";
    }
}

}
