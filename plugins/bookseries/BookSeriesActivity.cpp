#include "BookSeriesActivity.h"

#include <Epub.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryFormat.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "MappedInputManager.h"
#include "PluginListDraw.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char CACHE_PATH[] = "/.crosspoint/bookseries.cache";
constexpr char CACHE_DIR[] = "/.crosspoint";
constexpr char CACHE_MAGIC[] = "BS1\n";
constexpr size_t CACHE_MAX_BYTES = 393216;
constexpr unsigned long SCAN_SLICE_MS = 300;
constexpr unsigned long DRAW_INTERVAL_MS = 900;
constexpr int STATUS_HEIGHT = 28;
constexpr int NO_NUMBER = -1;

Rect listRectFor(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight -
                            metrics.verticalSpacing * 2 - STATUS_HEIGHT;
  return Rect{0, contentTop, renderer.getScreenWidth(), contentHeight};
}

bool isEpubPath(const std::string& path) {
  if (path.size() < 5) return false;
  const char* tail = path.c_str() + path.size() - 5;
  return (tail[0] == '.') && (tail[1] == 'e' || tail[1] == 'E') && (tail[2] == 'p' || tail[2] == 'P') &&
         (tail[3] == 'u' || tail[3] == 'U') && (tail[4] == 'b' || tail[4] == 'B');
}

std::string lowerKey(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) out.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c);
  return out;
}

std::string cleanField(std::string s) {
  for (auto& c : s) {
    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  }
  size_t start = 0;
  while (start < s.size() && s[start] == ' ') start++;
  size_t end = s.size();
  while (end > start && s[end - 1] == ' ') end--;
  return s.substr(start, end - start);
}

std::string formatNumber(const int milli) {
  if (milli < 0) return std::string();
  char buf[24];
  const int whole = milli / 1000;
  const int frac = milli % 1000;
  if (frac == 0) {
    snprintf(buf, sizeof(buf), "%d", whole);
  } else {
    char f[8];
    snprintf(f, sizeof(f), "%03d", frac);
    size_t n = 3;
    while (n > 1 && f[n - 1] == '0') n--;
    f[n] = '\0';
    snprintf(buf, sizeof(buf), "%d.%s", whole, f);
  }
  return buf;
}

void splitLines(const std::string& text, std::vector<std::string>& out) {
  size_t start = 0;
  while (start <= text.size()) {
    const size_t nl = text.find('\n', start);
    if (nl == std::string::npos) {
      out.push_back(text.substr(start));
      break;
    }
    out.push_back(text.substr(start, nl - start));
    start = nl + 1;
  }
}
}  // namespace

void BookSeriesActivity::onEnter() {
  Activity::onEnter();
  startScan(false);
  requestUpdate();
}

void BookSeriesActivity::onExit() {
  abortScan();
  Activity::onExit();
}

void BookSeriesActivity::showMessage(const char* text) {
  message = text;
  screen = Screen::MESSAGE;
  requestUpdate();
}

void BookSeriesActivity::startScan(const bool full) {
  abortScan();
  books.clear();
  seriesList.clear();
  order.clear();
  members.clear();
  oldCache.clear();
  outBuf.clear();
  activeSeries = -1;
  selected = 0;
  savedSelected = 0;
  scanPos = 0;
  scanTotal = 0;
  foundSeries = 0;

  if (!full) {
    HalFile in;
    if (Storage.openFileForRead("BSR", CACHE_PATH, in)) {
      char buf[512];
      while (in.available() > 0 && oldCache.size() < CACHE_MAX_BYTES) {
        const int n = in.read(buf, sizeof(buf));
        if (n <= 0) break;
        oldCache.append(buf, static_cast<size_t>(n));
      }
      in.close();
    }
    if (oldCache.compare(0, sizeof(CACHE_MAGIC) - 1, CACHE_MAGIC) != 0) oldCache.clear();
  }
  cachePos = 0;

  if (!idx.open(library::libraryIndexPath())) {
    oldCache.clear();
    showMessage("Library index not found.\nOpen Library once, then try again.");
    return;
  }
  scanOpen = true;
  scanTotal = idx.bookCount();

  Storage.mkdir(CACHE_DIR);
  outOpen = Storage.openFileForWrite("BSR", CACHE_PATH, outFile);
  outBuf = CACHE_MAGIC;
  screen = Screen::SCANNING;
  lastDraw = millis();
}

void BookSeriesActivity::flushOut(const bool force) {
  if (!outOpen) {
    outBuf.clear();
    return;
  }
  if (outBuf.empty() || (!force && outBuf.size() < 2048)) return;
  outFile.write(outBuf.data(), outBuf.size());
  outBuf.clear();
}

void BookSeriesActivity::abortScan() {
  if (outOpen) {
    flushOut(true);
    outFile.close();
    outOpen = false;
  }
  if (scanOpen) {
    idx.close();
    scanOpen = false;
  }
  std::string().swap(oldCache);
}

void BookSeriesActivity::addBook(const std::string& path, const std::string& title, const std::string& seriesName,
                                 const int number) {
  const std::string key = lowerKey(seriesName);
  int id = -1;
  for (size_t i = 0; i < seriesList.size(); i++) {
    if (seriesList[i].key == key) {
      id = static_cast<int>(i);
      break;
    }
  }
  if (id < 0) {
    if (seriesList.size() >= 0xFFFE) return;
    seriesList.push_back(Series{seriesName, key, 0});
    id = static_cast<int>(seriesList.size()) - 1;
    foundSeries = static_cast<uint16_t>(seriesList.size());
  }
  seriesList[static_cast<size_t>(id)].count++;
  books.push_back(Book{path, title, number, static_cast<uint16_t>(id)});
}

void BookSeriesActivity::processBook(const uint16_t ordinal) {
  library::ClixRecord rec{};
  std::string path;
  if (!idx.readRecord(ordinal, rec) || !idx.readPath(rec, path)) return;
  if (!isEpubPath(path)) return;

  const uint64_t hash = library::clixPathHash(path.data(), path.size());
  char prefix[56];
  snprintf(prefix, sizeof(prefix), "%08lx%08lx\t%lu\t", static_cast<unsigned long>(hash >> 32),
           static_cast<unsigned long>(hash & 0xFFFFFFFFUL), static_cast<unsigned long>(rec.fileSize));

  if (!oldCache.empty()) {
    const std::string pattern = std::string("\n") + prefix;
    size_t hit = oldCache.find(pattern, cachePos);
    if (hit == std::string::npos && cachePos > 0) hit = oldCache.find(pattern);
    if (hit != std::string::npos) {
      const size_t lineStart = hit + 1;
      const size_t lineEnd = oldCache.find('\n', lineStart);
      if (lineEnd != std::string::npos) {
        cachePos = lineEnd;
        const std::string rest = oldCache.substr(lineStart + std::string(prefix).size(),
                                                 lineEnd - lineStart - std::string(prefix).size());
        outBuf.append(oldCache, lineStart, lineEnd - lineStart + 1);
        if (rest != "-") {
          const size_t t1 = rest.find('\t');
          const size_t t2 = t1 == std::string::npos ? std::string::npos : rest.find('\t', t1 + 1);
          if (t2 != std::string::npos) {
            const int number = atoi(rest.substr(0, t1).c_str());
            addBook(path, rest.substr(t2 + 1), rest.substr(t1 + 1, t2 - t1 - 1), number);
          }
        }
        return;
      }
    }
  }

  Epub epub(path, CACHE_DIR);
  Epub::SyncMetadata meta;
  const bool ok = epub.loadSyncMetadata(meta);
  const std::string seriesName = ok ? cleanField(meta.series) : std::string();
  if (seriesName.empty()) {
    outBuf += prefix;
    outBuf += "-\n";
    return;
  }

  std::string title;
  if (!idx.readTitle(rec, title) || title.empty()) idx.readName(rec, title);
  title = cleanField(title);
  const int number = meta.seriesIndex.has_value() && *meta.seriesIndex >= 0.0f
                         ? static_cast<int>(*meta.seriesIndex * 1000.0f + 0.5f)
                         : NO_NUMBER;

  outBuf += prefix;
  outBuf += std::to_string(number);
  outBuf += '\t';
  outBuf += seriesName;
  outBuf += '\t';
  outBuf += title;
  outBuf += '\n';
  addBook(path, title, seriesName, number);
}

void BookSeriesActivity::scanStep() {
  const unsigned long start = millis();
  while (scanPos < scanTotal && millis() - start < SCAN_SLICE_MS) {
    processBook(scanPos);
    scanPos++;
    flushOut(false);
  }
  if (scanPos >= scanTotal) {
    finishScan();
    return;
  }
  if (millis() - lastDraw > DRAW_INTERVAL_MS) {
    lastDraw = millis();
    requestUpdate();
  }
}

void BookSeriesActivity::finishScan() {
  abortScan();
  RenderLock lock(*this);
  buildOrder();
  selected = 0;
  screen = Screen::SERIES;
  requestUpdate();
}

void BookSeriesActivity::buildOrder() {
  order.clear();
  for (size_t i = 0; i < seriesList.size(); i++) order.push_back(static_cast<uint16_t>(i));
  std::sort(order.begin(), order.end(), [this](const uint16_t a, const uint16_t b) {
    const int c = seriesList[a].key.compare(seriesList[b].key);
    if (c != 0) return descending ? c > 0 : c < 0;
    return a < b;
  });
}

void BookSeriesActivity::openSeries(const int seriesId) {
  RenderLock lock(*this);
  members.clear();
  for (size_t i = 0; i < books.size(); i++) {
    if (books[i].series == seriesId) members.push_back(static_cast<uint16_t>(i));
  }
  std::sort(members.begin(), members.end(), [this](const uint16_t a, const uint16_t b) {
    const Book& x = books[a];
    const Book& y = books[b];
    const int nx = x.number < 0 ? 0x7FFFFFFF : x.number;
    const int ny = y.number < 0 ? 0x7FFFFFFF : y.number;
    if (nx != ny) return nx < ny;
    return lowerKey(x.title) < lowerKey(y.title);
  });
  activeSeries = seriesId;
  savedSelected = selected;
  selected = 0;
  screen = Screen::BOOKS;
  requestUpdate();
}

int BookSeriesActivity::rowCount() const {
  if (screen == Screen::SERIES) return static_cast<int>(order.size()) + 2;
  if (screen == Screen::BOOKS) return static_cast<int>(members.size());
  return 0;
}

bool BookSeriesActivity::isSortRow(const int row) const { return screen == Screen::SERIES && row == 0; }

bool BookSeriesActivity::isRescanRow(const int row) const {
  return screen == Screen::SERIES && row == static_cast<int>(order.size()) + 1;
}

int BookSeriesActivity::seriesAtRow(const int row) const {
  if (screen != Screen::SERIES || row < 1 || row > static_cast<int>(order.size())) return -1;
  return order[static_cast<size_t>(row - 1)];
}

std::string BookSeriesActivity::rowTitle(const int row) const {
  if (screen == Screen::SERIES) {
    if (isSortRow(row)) return "Sort";
    if (isRescanRow(row)) return "Rescan library";
    return seriesList[static_cast<size_t>(seriesAtRow(row))].name;
  }
  const Book& book = books[members[static_cast<size_t>(row)]];
  const std::string number = formatNumber(book.number);
  return number.empty() ? book.title : number + ". " + book.title;
}

std::string BookSeriesActivity::rowValue(const int row) const {
  if (screen == Screen::SERIES) {
    if (isSortRow(row)) return descending ? "Z to A" : "A to Z";
    if (isRescanRow(row)) return "";
    const uint16_t count = seriesList[static_cast<size_t>(seriesAtRow(row))].count;
    return std::to_string(count) + (count == 1 ? " book" : " books");
  }
  return "";
}

void BookSeriesActivity::moveSelection(const int delta) {
  const int count = rowCount();
  if (count <= 0) return;
  selected = (selected + delta + count) % count;
}

void BookSeriesActivity::handleConfirm() {
  if (screen == Screen::MESSAGE) {
    finish();
    return;
  }
  if (screen == Screen::SERIES) {
    if (isSortRow(selected)) {
      descending = !descending;
      RenderLock lock(*this);
      buildOrder();
      requestUpdate();
      return;
    }
    if (isRescanRow(selected)) {
      RenderLock lock(*this);
      startScan(true);
      requestUpdate();
      return;
    }
    const int id = seriesAtRow(selected);
    if (id >= 0) openSeries(id);
    return;
  }
  if (screen == Screen::BOOKS && selected >= 0 && selected < static_cast<int>(members.size())) {
    const std::string path = books[members[static_cast<size_t>(selected)]].path;
    onSelectBook(path);
  }
}

void BookSeriesActivity::handleBack() {
  if (screen == Screen::BOOKS) {
    RenderLock lock(*this);
    screen = Screen::SERIES;
    selected = savedSelected;
    requestUpdate();
    return;
  }
  finish();
}

void BookSeriesActivity::loop() {
  const bool back = mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasBackGesture();

  if (screen == Screen::SCANNING) {
    if (back) {
      abortScan();
      finish();
      return;
    }
    scanStep();
    return;
  }

  if (back) {
    handleBack();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
      mappedInput.wasPressed(MappedInputManager::Button::Power)) {
    handleConfirm();
    return;
  }
  if (screen == Screen::MESSAGE) return;

  {
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
      const int rowHeight = renderer.getLineHeight(UI_10_FONT_ID) + 14;
      int step = listRectFor(renderer).height / rowHeight - 1;
      if (step < 1) step = 1;
      const int dir = swipe == MappedInputManager::SwipeDir::Up ? 1 : -1;
      const int target = std::max(0, std::min(rowCount() - 1, selected + dir * step));
      if (target != selected) {
        selected = target;
        requestUpdate();
      }
      return;
    }
  }
  {
    int touched = 0;
    const auto touch = pluginui::listTouch(mappedInput, renderer, listRectFor(renderer), rowCount(), selected, touched);
    if (touch != MappedInputManager::RowTouch::None) {
      selected = touched;
      if (touch == MappedInputManager::RowTouch::Tap) {
        handleConfirm();
      } else {
        requestUpdate();
      }
      return;
    }
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

void BookSeriesActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  std::string headerTitle = "Book Series";
  std::string headerSub;
  if (screen == Screen::SERIES) {
    headerSub = std::to_string(order.size()) + " series";
  } else if (screen == Screen::BOOKS && activeSeries >= 0) {
    headerTitle = seriesList[static_cast<size_t>(activeSeries)].name;
    headerSub = std::to_string(members.size()) + (members.size() == 1 ? " book" : " books");
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, headerTitle.c_str(),
                 headerSub.empty() ? nullptr : headerSub.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  if (screen == Screen::SCANNING) {
    char buf[64];
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - lineHeight * 2, "Reading series info");
    snprintf(buf, sizeof(buf), "%u of %u books", static_cast<unsigned>(scanPos), static_cast<unsigned>(scanTotal));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, buf);
    snprintf(buf, sizeof(buf), "%u series found", static_cast<unsigned>(foundSeries));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + lineHeight * 2, buf);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (screen == Screen::MESSAGE) {
    std::vector<std::string> lines;
    splitLines(message, lines);
    int y = pageHeight / 2 - static_cast<int>(lines.size()) * lineHeight;
    for (const auto& line : lines) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str());
      y += lineHeight * 2;
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2 - STATUS_HEIGHT;
  const Rect listRect{0, contentTop, pageWidth, contentHeight};
  pluginui::drawList(
      renderer, listRect, rowCount(), selected, [this](int i) { return rowTitle(i); },
      [this](int i) { return rowValue(i); }, nullptr);

  if (screen == Screen::SERIES && order.empty()) {
    const int rowHeight = lineHeight + 14;
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + rowHeight * 3, "No series found in your EPUBs");
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
