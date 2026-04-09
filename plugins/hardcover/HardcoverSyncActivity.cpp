#include "HardcoverSyncActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "HardcoverPlugin.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void HardcoverSyncActivity::onEnter() {
  Activity::onEnter();
  state   = SYNCING;
  result  = HardcoverPlugin::SyncResult::OK;
  requestUpdate();
}

void HardcoverSyncActivity::onExit() { Activity::onExit(); }

void HardcoverSyncActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth  = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Hardcover");

  if (state == SYNCING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Syncing...");
    renderer.displayBuffer();
    return;
  }

  // DONE state - show result
  const char* line1 = nullptr;
  const char* line2 = nullptr;

  switch (result) {
    case HardcoverPlugin::SyncResult::OK:
      line1 = "Sync complete.";
      break;
    case HardcoverPlugin::SyncResult::NO_BOOKS:
      line1 = "Nothing to sync.";
      line2 = "No in-progress epubs with ISBN found.";
      break;
    case HardcoverPlugin::SyncResult::NO_WIFI:
      line1 = "Not connected to WiFi.";
      line2 = "Connect to WiFi and try again.";
      break;
    case HardcoverPlugin::SyncResult::NO_TOKEN:
      line1 = "API token not set.";
      line2 = "Enter token in Settings > Plugins > Hardcover.";
      break;
    case HardcoverPlugin::SyncResult::API_ERROR:
      line1 = "Sync failed.";
      line2 = "Check serial output for details.";
      break;
  }

  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - (line2 ? 15 : 0), line1, true,
                             result == HardcoverPlugin::SyncResult::OK ? EpdFontFamily::BOLD
                                                                       : EpdFontFamily::REGULAR);
  if (line2) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 15, line2);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void HardcoverSyncActivity::doSync() {
  result = HardcoverPlugin::syncProgress();
  state  = DONE;
  requestUpdate();
}

void HardcoverSyncActivity::loop() {
  if (state == SYNCING) {
    {
      RenderLock lock(*this);
    }
    requestUpdateAndWait();
    doSync();
    return;
  }

  if (state == DONE) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }
}
