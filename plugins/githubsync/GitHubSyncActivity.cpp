#include "GitHubSyncActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "GitHubSyncPlugin.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void GitHubSyncActivity::onEnter() {
  Activity::onEnter();
  state  = SYNCING;
  result = GitHubSyncPlugin::SyncResult::OK;
  requestUpdate();
}

void GitHubSyncActivity::onExit() { Activity::onExit(); }

void GitHubSyncActivity::render(RenderLock&&) {
  const auto& metrics   = UITheme::getInstance().getMetrics();
  const auto pageWidth  = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "GitHub Sync");

  if (state == SYNCING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Syncing...");
    renderer.displayBuffer();
    return;
  }

  const char* line1 = nullptr;
  const char* line2 = nullptr;

  switch (result) {
    case GitHubSyncPlugin::SyncResult::OK:
      line1 = "Sync complete.";
      break;
    case GitHubSyncPlugin::SyncResult::NO_WIFI:
      line1 = "Not connected to WiFi.";
      line2 = "Connect to WiFi and try again.";
      break;
    case GitHubSyncPlugin::SyncResult::GIT_ERROR:
      line1 = "Sync failed.";
      line2 = "Check GitHub URL and PAT in Settings > Plugins.";
      break;
  }

  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - (line2 ? 15 : 0), line1, true,
                             result == GitHubSyncPlugin::SyncResult::OK ? EpdFontFamily::BOLD
                                                                        : EpdFontFamily::REGULAR);
  if (line2)
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 15, line2);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void GitHubSyncActivity::doSync() {
  result = GitHubSyncPlugin::sync();
  state  = DONE;
  requestUpdate();
}

void GitHubSyncActivity::loop() {
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
