#include "AudiobookshelfActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <HalStorage.h>
#include <WiFi.h>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/CrossPointWebServerActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "PluginListDraw.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
bool anyButtonPressedImpl(const MappedInputManager& in) {
  using B = MappedInputManager::Button;
  return in.wasPressed(B::Back) || in.wasPressed(B::Confirm) || in.wasPressed(B::Left) || in.wasPressed(B::Right) ||
         in.wasPressed(B::Up) || in.wasPressed(B::Down) || in.wasPressed(B::Power);
}
constexpr unsigned long IDLE_TIMEOUT_MS = 5UL * 60UL * 1000UL;
constexpr int STATUS_HEIGHT = 28;

const char* shelfLabel(const std::string& id) {
  if (id == "continue-reading") return "Continue Reading";
  if (id == "recently-added") return "Recently Added";
  if (id == "recent-series") return "Recent Series";
  if (id == "discover") return "Discover";
  return nullptr;
}
}

void AudiobookshelfActivity::onEnter() {
  Activity::onEnter();
  AudiobookshelfPlugin::loadConfig(cfg);
  lastInput = millis();
  state = State::CONNECTING;
  busyText = "Connecting to WiFi...";
  requestUpdate();

  if (WiFi.status() == WL_CONNECTED) {
    afterWifi();
    return;
  }
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                             finish();
                             return;
                           }
                           afterWifi();
                         });
}

void AudiobookshelfActivity::onExit() {
  Activity::onExit();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    if (!openPath.empty()) {
      APP_STATE.openEpubPath = openPath;
      APP_STATE.saveToFile();
      silentRestartToReader();
    } else {
      silentRestart();
    }
  }
}

void AudiobookshelfActivity::afterWifi() {
  lastInput = millis();
  setBusy("Connecting to server...");
  pending = Pending::START;
}

void AudiobookshelfActivity::setBusy(const std::string& text) {
  state = State::WORKING;
  busyText = text;
  requestUpdate();
}

int AudiobookshelfActivity::rowCount() const {
  return state == State::FORM ? FORM_COUNT : static_cast<int>(rows.size());
}

bool AudiobookshelfActivity::selectable(int index) const {
  if (state == State::FORM) return true;
  if (index < 0 || index >= static_cast<int>(rows.size())) return false;
  return rows[index].kind != RowKind::HEADING;
}

void AudiobookshelfActivity::moveSelection(int delta) {
  const int count = rowCount();
  if (count == 0) return;
  int idx = selected;
  for (int i = 0; i < count; ++i) {
    idx = (idx + delta + count) % count;
    if (selectable(idx)) {
      selected = idx;
      return;
    }
  }
}

AbsResult AudiobookshelfActivity::startSession() {
  AbsResult r = AbsResult::AUTH_ERROR;
  if (!cfg.token.empty()) r = AudiobookshelfPlugin::authorize(cfg);
  if (r == AbsResult::AUTH_ERROR && !cfg.user.empty() && !cfg.pass.empty()) {
    r = AudiobookshelfPlugin::login(cfg);
    if (r == AbsResult::OK) {
      persistLogin();
      r = AudiobookshelfPlugin::authorize(cfg);
    }
  }
  if (r == AbsResult::OK) r = AudiobookshelfPlugin::loadShelves(cfg, shelves);
  return r;
}

void AudiobookshelfActivity::persistLogin() {
  const std::string sessionPass = cfg.pass;
  if (!cfg.savePass) cfg.pass.clear();
  AudiobookshelfPlugin::saveConfig(cfg);
  cfg.pass = sessionPass;
}

void AudiobookshelfActivity::runStart() {
  if (cfg.server.empty()) {
    enterForm("");
    return;
  }
  const AbsResult r = startSession();
  if (r == AbsResult::OK) {
    enterBrowse();
    return;
  }
  if (r == AbsResult::AUTH_ERROR) cfg.token.clear();
  enterForm(cfg.user.empty() ? "" : AudiobookshelfPlugin::message(r));
}

void AudiobookshelfActivity::runLogin() {
  if (cfg.server.empty() || cfg.user.empty() || cfg.pass.empty()) {
    enterForm("Server, user and password required");
    return;
  }
  AbsResult r = AudiobookshelfPlugin::login(cfg);
  if (r == AbsResult::OK) {
    persistLogin();
    r = AudiobookshelfPlugin::authorize(cfg);
    if (r == AbsResult::OK) r = AudiobookshelfPlugin::loadShelves(cfg, shelves);
  }
  if (r == AbsResult::OK) {
    enterBrowse();
    return;
  }
  enterForm(AudiobookshelfPlugin::message(r));
}

void AudiobookshelfActivity::runSearch() {
  const AbsResult r = AudiobookshelfPlugin::search(cfg, searchQuery, searchResults);
  state = State::BROWSE;
  if (r != AbsResult::OK) {
    statusText = AudiobookshelfPlugin::message(r);
    requestUpdate();
    return;
  }
  searchShown = searchQuery;
  savedSelected = selected;
  view = View::SEARCH;
  statusText.clear();
  buildRows();
  selected = 0;
  moveSelection(1);
  requestUpdate();
}

void AudiobookshelfActivity::runDownload() {
  std::string path = AudiobookshelfPlugin::bookPath(downloadItem);
  if (!Storage.exists(path.c_str())) {
    busyText = "Checking library...";
    requestUpdateAndWait();
    const std::string existing = AudiobookshelfPlugin::findExistingBook(downloadItem);
    if (!existing.empty()) path = existing;
  }
  if (!Storage.exists(path.c_str())) {
    int lastStep = -1;
    const AbsResult r = AudiobookshelfPlugin::downloadBook(
        cfg, downloadItem.id, path, [this, &lastStep](size_t done, size_t total) {
          if (total == 0) return;
          const int step = static_cast<int>((done * 100 / total) / 20);
          if (step == lastStep) return;
          lastStep = step;
          busyText = "Downloading " + std::to_string(step * 20) + "%";
          requestUpdateAndWait();
        });
    if (r != AbsResult::OK) {
      state = State::BROWSE;
      statusText = AudiobookshelfPlugin::message(r);
      requestUpdate();
      return;
    }
  }
  openPath = path;
  finish();
}

void AudiobookshelfActivity::enterBrowse() {
  state = State::BROWSE;
  view = View::MAIN;
  statusText.clear();
  buildRows();
  selected = 0;
  moveSelection(1);
  lastInput = millis();
  requestUpdate();
}

void AudiobookshelfActivity::enterForm(const std::string& status) {
  state = State::FORM;
  statusText = status;
  selected = FORM_SERVER;
  lastInput = millis();
  requestUpdate();
}

void AudiobookshelfActivity::buildRows() {
  rows.clear();
  if (view == View::MAIN) {
    rows.push_back({RowKind::SEARCH_FIELD, "", {}});
    rows.push_back({RowKind::SEARCH_OK, "OK", {}});
    const char* order[] = {"continue-reading", "recently-added", "recent-series", "discover"};
    for (const char* id : order) {
      for (const auto& shelf : shelves) {
        if (shelf.id != id || shelf.items.empty()) continue;
        rows.push_back({RowKind::HEADING, std::string(shelfLabel(id)) + ":", {}});
        for (const auto& item : shelf.items) {
          rows.push_back({item.isSeries ? RowKind::SERIES : RowKind::ITEM, AbsParse::itemLabel(item), item});
        }
      }
    }
    rows.push_back({RowKind::LOGOUT, "Logout", {}});
    return;
  }
  if (view == View::SEARCH) {
    rows.push_back({RowKind::HEADING, "Results for \"" + searchShown + "\":", {}});
    if (searchResults.empty()) rows.push_back({RowKind::HEADING, "No ebooks found", {}});
    for (const auto& item : searchResults) rows.push_back({RowKind::ITEM, AbsParse::itemLabel(item), item});
    return;
  }
  rows.push_back({RowKind::HEADING, activeSeries.title + ":", {}});
  for (const auto& item : activeSeries.books) rows.push_back({RowKind::ITEM, AbsParse::itemLabel(item), item});
}

void AudiobookshelfActivity::editText(const char* title, const std::string& initial, size_t maxLen, InputType inputType,
                                      std::function<void(const std::string&)> onDone) {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, title, initial, maxLen, inputType),
      [this, onDone](const ActivityResult& result) {
        lastInput = millis();
        if (!result.isCancelled) onDone(std::get<KeyboardResult>(result.data).text);
        requestUpdate();
      });
}

void AudiobookshelfActivity::doLogout() {
  cfg.token.clear();
  if (!cfg.savePass) cfg.pass.clear();
  AudiobookshelfPlugin::saveConfig(cfg);
  shelves.clear();
  searchResults.clear();
  enterForm("Logged out");
}

void AudiobookshelfActivity::handleFormConfirm() {
  switch (selected) {
    case FORM_SERVER:
      editText("Audiobookshelf Server", cfg.server, 127, InputType::Url,
               [this](const std::string& t) { cfg.server = t; });
      break;
    case FORM_USER:
      editText("User Name", cfg.user, 63, InputType::Text,
               [this](const std::string& t) { cfg.user = t; });
      break;
    case FORM_PASS:
      editText("Password", cfg.pass, 127, InputType::Password,
               [this](const std::string& t) { cfg.pass = t; });
      break;
    case FORM_SAVE:
      cfg.savePass = !cfg.savePass;
      requestUpdate();
      break;
    case FORM_PC:
      AudiobookshelfPlugin::saveConfig(cfg);
      startActivityForResult(std::make_unique<CrossPointWebServerActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               AudiobookshelfPlugin::loadConfig(cfg);
                               lastInput = millis();
                               requestUpdate();
                             });
      break;
    case FORM_OK:
      statusText.clear();
      setBusy("Signing in...");
      pending = Pending::LOGIN;
      break;
    case FORM_CANCEL:
      finish();
      break;
  }
}

void AudiobookshelfActivity::handleBrowseConfirm() {
  if (selected < 0 || selected >= static_cast<int>(rows.size())) return;
  const Row row = rows[selected];
  switch (row.kind) {
    case RowKind::SEARCH_FIELD:
      editText("Search", searchQuery, 63, InputType::Text,
               [this](const std::string& t) { searchQuery = t; });
      break;
    case RowKind::SEARCH_OK:
      if (searchQuery.empty()) {
        statusText = "Type a search term first";
        requestUpdate();
        break;
      }
      statusText.clear();
      setBusy("Searching...");
      pending = Pending::SEARCH;
      break;
    case RowKind::ITEM:
      downloadItem = row.item;
      statusText.clear();
      setBusy("Downloading...");
      pending = Pending::DOWNLOAD;
      break;
    case RowKind::SERIES:
      activeSeries = row.item;
      savedSelected = selected;
      view = View::SERIES;
      statusText.clear();
      buildRows();
      selected = 0;
      moveSelection(1);
      requestUpdate();
      break;
    case RowKind::LOGOUT:
      doLogout();
      break;
    case RowKind::HEADING:
      break;
  }
}

void AudiobookshelfActivity::handleConfirm() {
  if (state == State::FORM) {
    handleFormConfirm();
  } else if (state == State::BROWSE) {
    handleBrowseConfirm();
  }
}

void AudiobookshelfActivity::handleBack() {
  if (state == State::BROWSE && view != View::MAIN) {
    view = View::MAIN;
    statusText.clear();
    buildRows();
    selected = savedSelected;
    if (!selectable(selected)) moveSelection(1);
    requestUpdate();
    return;
  }
  finish();
}

void AudiobookshelfActivity::runPending() {
  const Pending p = pending;
  pending = Pending::NONE;
  switch (p) {
    case Pending::START:
      runStart();
      break;
    case Pending::LOGIN:
      runLogin();
      break;
    case Pending::SEARCH:
      runSearch();
      break;
    case Pending::DOWNLOAD:
      runDownload();
      break;
    case Pending::NONE:
      break;
  }
  lastInput = millis();
}

void AudiobookshelfActivity::loop() {
  if (pending != Pending::NONE) {
    requestUpdateAndWait();
    runPending();
    return;
  }

  if (anyButtonPressedImpl(mappedInput)) {
    lastInput = millis();
  } else if (millis() - lastInput > IDLE_TIMEOUT_MS) {
    finish();
    return;
  }

  if (state != State::FORM && state != State::BROWSE) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    handleBack();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
      mappedInput.wasPressed(MappedInputManager::Button::Power)) {
    handleConfirm();
    return;
  }
  buttonNavigator.onNext([this] {
    moveSelection(1);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    moveSelection(-1);
    requestUpdate();
  });
}

void AudiobookshelfActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const std::string host = AbsParse::hostLabel(cfg.server);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Audiobookshelf",
                 host.empty() ? nullptr : host.c_str());

  if (state == State::CONNECTING || state == State::WORKING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, busyText.c_str());
    renderer.displayBuffer();
    return;
  }

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2 - STATUS_HEIGHT;
  const Rect listRect{0, contentTop, pageWidth, contentHeight};

  if (state == State::FORM) {
    pluginui::drawList(
        renderer, listRect, FORM_COUNT, selected,
        [this](int i) -> std::string {
          switch (i) {
            case FORM_SERVER:
              return "Server";
            case FORM_USER:
              return "User Name";
            case FORM_PASS:
              return "Password";
            case FORM_SAVE:
              return std::string(cfg.savePass ? "[x]" : "[ ]") + " Save password to SD card (plain text)";
            case FORM_PC:
              return "Configure with PC";
            case FORM_OK:
              return "OK";
            default:
              return "Cancel";
          }
        },
        [this](int i) -> std::string {
          if (i == FORM_SERVER) return cfg.server.empty() ? "(not set)" : cfg.server;
          if (i == FORM_USER) return cfg.user.empty() ? "(not set)" : cfg.user;
          if (i == FORM_PASS) return cfg.pass.empty() ? "(not set)" : std::string(cfg.pass.size() < 12 ? cfg.pass.size() : 12, '*');
          return "";
        },
        nullptr);
  } else {
    pluginui::drawList(
        renderer, listRect, static_cast<int>(rows.size()), selected,
        [this](int i) -> std::string {
          const Row& row = rows[i];
          if (row.kind == RowKind::SEARCH_FIELD) return "Search:";
          return row.text;
        },
        [this](int i) -> std::string {
          if (rows[i].kind == RowKind::SEARCH_FIELD) return searchQuery.empty() ? "(press Select)" : searchQuery;
          return "";
        },
        [this](int i) { return rows[i].kind == RowKind::HEADING; });
  }

  if (!statusText.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + contentHeight + 4, statusText.c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
