#pragma once

#include <HalStorage.h>
#include <LibraryIndexFile.h>

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class BookSeriesActivity final : public Activity {
 public:
  explicit BookSeriesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BookSeries", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Screen { SCANNING, SERIES, BOOKS, MESSAGE };

  struct Book {
    std::string path;
    std::string title;
    int number;
    uint16_t series;
  };

  struct Series {
    std::string name;
    std::string key;
    uint16_t count;
  };

  ButtonNavigator buttonNavigator;
  Screen screen = Screen::SCANNING;

  std::vector<Book> books;
  std::vector<Series> seriesList;
  std::vector<uint16_t> order;
  std::vector<uint16_t> members;

  library::LibraryIndexFile idx;
  HalFile outFile;
  std::string oldCache;
  std::string outBuf;
  std::string message;

  uint16_t scanPos = 0;
  uint16_t scanTotal = 0;
  unsigned long lastDraw = 0;
  uint16_t foundSeries = 0;
  size_t cachePos = 0;
  bool scanOpen = false;
  bool outOpen = false;
  bool descending = false;
  int activeSeries = -1;
  int selected = 0;
  int savedSelected = 0;

  void startScan(bool full);
  void scanStep();
  void processBook(uint16_t ordinal);
  void finishScan();
  void abortScan();
  void flushOut(bool force);
  void addBook(const std::string& path, const std::string& title, const std::string& seriesName, int number);
  void buildOrder();
  void openSeries(int seriesId);
  void showMessage(const char* text);

  int rowCount() const;
  bool isSortRow(int row) const;
  bool isRescanRow(int row) const;
  int seriesAtRow(int row) const;
  void moveSelection(int delta);
  void handleConfirm();
  void handleBack();
  std::string rowTitle(int row) const;
  std::string rowValue(int row) const;
};
