#pragma once

#include <string>
#include <vector>

#include "AudiobookshelfPlugin.h"
#include "activities/Activity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "util/ButtonNavigator.h"

class AudiobookshelfActivity final : public Activity {
 public:
  explicit AudiobookshelfActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Audiobookshelf", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State { CONNECTING, FORM, WORKING, BROWSE };
  enum class View { MAIN, SEARCH, SERIES };
  enum class Pending { NONE, START, LOGIN, SEARCH, DOWNLOAD };
  enum class RowKind { SEARCH_FIELD, SEARCH_OK, HEADING, ITEM, SERIES, LOGOUT };

  struct Row {
    RowKind kind;
    std::string text;
    AbsItem item;
  };

  static constexpr int FORM_SERVER = 0;
  static constexpr int FORM_USER = 1;
  static constexpr int FORM_PASS = 2;
  static constexpr int FORM_SAVE = 3;
  static constexpr int FORM_PC = 4;
  static constexpr int FORM_OK = 5;
  static constexpr int FORM_CANCEL = 6;
  static constexpr int FORM_COUNT = 7;

  ButtonNavigator buttonNavigator;
  State state = State::CONNECTING;
  View view = View::MAIN;
  Pending pending = Pending::NONE;

  AbsConfig cfg;
  std::vector<AbsShelf> shelves;
  std::vector<AbsItem> searchResults;
  std::vector<Row> rows;
  AbsItem activeSeries;
  AbsItem downloadItem;
  std::string searchQuery;
  std::string searchShown;
  std::string busyText;
  std::string statusText;
  std::string openPath;

  int selected = 0;
  int savedSelected = 0;
  unsigned long lastInput = 0;

  void afterWifi();
  void runPending();
  void runStart();
  void runLogin();
  void runSearch();
  void runDownload();
  AbsResult startSession();
  void persistLogin();
  void setBusy(const std::string& text);
  void enterBrowse();
  void enterForm(const std::string& status);
  void buildRows();
  void moveSelection(int delta);
  bool selectable(int index) const;
  void handleConfirm();
  void handleFormConfirm();
  void handleBrowseConfirm();
  void handleBack();
  void editText(const char* title, const std::string& initial, size_t maxLen, InputType inputType,
                std::function<void(const std::string&)> onDone);
  void doLogout();
  int rowCount() const;
};
