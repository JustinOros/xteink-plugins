#include "HardcoverSyncActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include "HardcoverPlugin.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void HardcoverSyncActivity::onEnter() {
  Activity::onEnter();
  result = HardcoverPlugin::SyncResult::OK;
  startWifiThenSync();
}

void HardcoverSyncActivity::onExit() { Activity::onExit(); }

void HardcoverSyncActivity::startWifiThenSync() {
  if (WiFi.status() == WL_CONNECTED) {
    state = SYNCING;
    requestUpdate();
    return;
  }

  state = CONNECTING;
  requestUpdate();

  startActivityForResult(
      std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
      [this](const ActivityResult&) {
        if (WiFi.status() != WL_CONNECTED) {
          result = HardcoverPlugin::SyncResult::NO_WIFI;
          state  = DONE;
          requestUpdate();
          return;
        }
        state = SYNCING;
        requestUpdate();
      });
}

void HardcoverSyncActivity::render(RenderLock&&) {
  const auto& metrics   = UITheme::getInstance().getMetrics();
  const auto pageWidth  = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Hardcover");

  if (state == CONNECTING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Connecting to WiFi...");
    renderer.displayBuffer();
    return;
  }

  if (state == SYNCING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Syncing...");
    renderer.displayBuffer();
    return;
  }

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
  if (line2)
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 15, line2);

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
    if (mappedInput.wasPressed(MappedInputManager::Button::Back))
      finish();
    return;
  }
}
