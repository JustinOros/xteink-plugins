#pragma once

#include <GfxRenderer.h>

#include <functional>
#include <string>

#include "components/UITheme.h"
#include "fontIds.h"

namespace pluginui {

using RowText = std::function<std::string(int)>;
using RowFlag = std::function<bool(int)>;

inline int firstVisibleRow(int count, int selected, int visible) {
  if (visible <= 0 || count <= visible) return 0;
  int top = (selected / visible) * visible;
  if (top + visible > count) top = count - visible;
  return top < 0 ? 0 : top;
}

inline void drawList(GfxRenderer& renderer, const Rect& rect, int count, int selected, const RowText& title,
                     const RowText& value, const RowFlag& dimmed) {
  const int fontId = UI_10_FONT_ID;
  const int lineHeight = renderer.getLineHeight(fontId);
  const int rowHeight = lineHeight + 14;
  const int visible = rect.height / rowHeight;
  const int top = firstVisibleRow(count, selected, visible);
  const int pad = 12;

  for (int i = 0; i < visible && top + i < count; i++) {
    const int index = top + i;
    const int y = rect.y + i * rowHeight;
    const bool isSelected = index == selected;
    const bool isDim = dimmed && dimmed(index);

    if (isSelected) renderer.fillRect(rect.x + 4, y, rect.width - 8, rowHeight - 2, true);
    const bool ink = !isSelected;

    std::string val = value ? value(index) : std::string();
    int valueWidth = 0;
    if (!val.empty()) {
      val = renderer.truncatedText(fontId, val.c_str(), rect.width / 2 - pad);
      valueWidth = renderer.getTextWidth(fontId, val.c_str());
    }
    const int titleMax = rect.width - pad * 2 - valueWidth - (valueWidth ? pad : 0);
    const std::string label = renderer.truncatedText(
        fontId, title(index).c_str(), titleMax, isDim ? EpdFontFamily::ITALIC : EpdFontFamily::REGULAR);
    renderer.drawText(fontId, rect.x + pad, y + 6, label.c_str(), ink,
                      isDim ? EpdFontFamily::ITALIC : EpdFontFamily::REGULAR);
    if (!val.empty()) renderer.drawText(fontId, rect.x + rect.width - pad - valueWidth, y + 6, val.c_str(), ink);
  }

  if (count > visible && visible > 0) {
    const int trackX = rect.x + rect.width - 3;
    const int barHeight = rect.height * visible / count;
    const int barY = rect.y + (rect.height - barHeight) * top / (count - visible);
    renderer.fillRect(trackX, barY, 3, barHeight, true);
  }
}

}  // namespace pluginui
