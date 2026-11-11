#include "MinesweeperActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <math.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

using namespace minesweeper;

const MinesweeperActivity::Difficulty MinesweeperActivity::LEVELS[MinesweeperActivity::LEVEL_COUNT] = {
    {"Easy", "9 x 9 grid, 10 mines", 9, 9, 10},
    {"Medium", "12 x 14 grid, 30 mines", 12, 14, 30},
    {"Hard", "14 x 16 grid, 45 mines", 14, 16, 45},
};

void MinesweeperActivity::onEnter() {
    Activity::onEnter();

    screenW_ = renderer.getScreenWidth();
    screenH_ = renderer.getScreenHeight();
    randomSeed(millis());

    screen_ = Screen::MENU;
    menuIndex_ = 0;
    requestUpdate();
}

void MinesweeperActivity::onExit() {
    Activity::onExit();
}

void MinesweeperActivity::computeLayout() {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int usableH = screenH_ - metrics.buttonHintsHeight;
    barY_ = usableH - BAR_H - 8;

    const int availH = barY_ - 10 - BOARD_TOP;
    const int byW = (screenW_ - 16) / board_.cols();
    const int byH = availH / board_.rows();
    cellS_ = byW < byH ? byW : byH;

    const int boardW = cellS_ * board_.cols();
    const int boardH = cellS_ * board_.rows();
    boardLeft_ = (screenW_ - boardW) / 2;
    boardTop_ = BOARD_TOP + (availH - boardH) / 2;
}

void MinesweeperActivity::startGame(int level) {
    level_ = level;
    board_.reset(LEVELS[level].cols, LEVELS[level].rows, LEVELS[level].mines);
    flagMode_ = false;
    cursor_ = (board_.rows() / 2) * board_.cols() + board_.cols() / 2;
    startMs_ = 0;
    endMs_ = 0;
    endedAt_ = 0;
    computeLayout();
    screen_ = Screen::GAME;
    requestUpdate();
}

int MinesweeperActivity::cellAt(int x, int y) const {
    if (x < boardLeft_ || y < boardTop_) return -1;
    const int c = (x - boardLeft_) / cellS_;
    const int r = (y - boardTop_) / cellS_;
    if (c >= board_.cols() || r >= board_.rows()) return -1;
    return r * board_.cols() + c;
}

void MinesweeperActivity::afterAction(Outcome o) {
    if (o != Outcome::NONE && endedAt_ == 0) {
        endMs_ = millis();
        endedAt_ = millis();
    }
    requestUpdate();
}

void MinesweeperActivity::dig(int idx) {
    if (over()) return;
    Outcome o = Outcome::NONE;
    if (board_.state(idx) == Board::HIDDEN) {
        if (!board_.started()) startMs_ = millis();
        o = board_.reveal(idx);
    } else if (board_.state(idx) == Board::OPEN) {
        o = board_.chord(idx);
    }
    afterAction(o);
}

void MinesweeperActivity::flag(int idx) {
    if (over()) return;
    if (board_.state(idx) == Board::OPEN) {
        afterAction(board_.chord(idx));
        return;
    }
    board_.toggleFlag(idx);
    requestUpdate();
}

void MinesweeperActivity::handleMenu() {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        finish();
        return;
    }

    int tx = 0;
    int ty = 0;
    if (mappedInput.wasScreenTapped(tx, ty)) {
        if (ty >= MENU_TOP) {
            const int row = (ty - MENU_TOP) / MENU_ROW_H;
            if (row >= 0 && row < LEVEL_COUNT) {
                menuIndex_ = row;
                startGame(row);
            }
        }
        return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        menuIndex_ = (menuIndex_ + LEVEL_COUNT - 1) % LEVEL_COUNT;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
               mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        menuIndex_ = (menuIndex_ + 1) % LEVEL_COUNT;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
               mappedInput.wasPressed(MappedInputManager::Button::Power)) {
        startGame(menuIndex_);
    }
}

void MinesweeperActivity::handleGame() {
    int tx = 0;
    int ty = 0;

    if (mappedInput.wasScreenLongPress(tx, ty)) {
        const int idx = cellAt(tx, ty);
        if (idx >= 0 && !over()) {
            cursor_ = idx;
            flag(idx);
        }
        return;
    }

    if (mappedInput.wasScreenTapped(tx, ty)) {
        if (over()) {
            if (millis() - endedAt_ > 1000) {
                endedAt_ = 0;
                screen_ = Screen::MENU;
                menuIndex_ = level_;
                requestUpdate();
            }
            return;
        }
        if (ty >= barY_ && ty < barY_ + BAR_H) {
            flagMode_ = tx >= screenW_ / 2;
            requestUpdate();
            return;
        }
        const int idx = cellAt(tx, ty);
        if (idx >= 0) {
            cursor_ = idx;
            if (flagMode_) {
                flag(idx);
            } else {
                dig(idx);
            }
        }
        return;
    }

    if (over()) {
        if (millis() - endedAt_ > 1000 && (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
                                           mappedInput.wasPressed(MappedInputManager::Button::Back))) {
            endedAt_ = 0;
            screen_ = Screen::MENU;
            menuIndex_ = level_;
            requestUpdate();
        }
        return;
    }

    const int cols = board_.cols();
    const int rows = board_.rows();
    const int r = cursor_ / cols;
    const int c = cursor_ % cols;
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        if (r > 0) cursor_ -= cols;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        if (r < rows - 1) cursor_ += cols;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        if (c > 0) cursor_ -= 1;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        if (c < cols - 1) cursor_ += 1;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        dig(cursor_);
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        flag(cursor_);
    }
}

void MinesweeperActivity::loop() {
    const bool backGesture = mappedInput.wasBackGesture();
    const bool power = mappedInput.wasPressed(MappedInputManager::Button::Power);
    if (backGesture || (screen_ == Screen::GAME && power)) {
        while (mappedInput.isPressed(MappedInputManager::Button::Power)) {
            mappedInput.update();
            delay(10);
        }
        finish();
        return;
    }

    if (screen_ == Screen::MENU) {
        handleMenu();
    } else {
        handleGame();
    }
}

void MinesweeperActivity::fillCircle(int cx, int cy, int r, bool black) const {
    if (r < 2) return;
    constexpr int N = 20;
    int xs[N];
    int ys[N];
    for (int i = 0; i < N; i++) {
        const float a = (float)i * 6.2831853f / (float)N;
        xs[i] = cx + (int)lroundf(cosf(a) * (float)r);
        ys[i] = cy + (int)lroundf(sinf(a) * (float)r);
    }
    renderer.fillPolygon(xs, ys, N, black);
}

void MinesweeperActivity::drawFlag(int cx, int cy, int s) const {
    const int top = cy - s * 6 / 10;
    const int bottom = cy + s * 6 / 10;
    renderer.fillRect(cx - 2, top, 3, bottom - top, true);
    int xs[3] = {cx, cx + s * 7 / 10, cx};
    int ys[3] = {top, top + s * 3 / 10, top + s * 6 / 10};
    renderer.fillPolygon(xs, ys, 3, true);
    renderer.fillRect(cx - s * 4 / 10, bottom, s * 8 / 10, 3, true);
}

void MinesweeperActivity::drawMine(int cx, int cy, int s, bool inverted) const {
    const bool ink = !inverted;
    const int r = s * 4 / 10;
    fillCircle(cx, cy, r, ink);
    const int spike = s * 7 / 10;
    renderer.drawLine(cx - spike, cy, cx + spike, cy, 2, ink);
    renderer.drawLine(cx, cy - spike, cx, cy + spike, 2, ink);
    renderer.drawLine(cx - spike * 7 / 10, cy - spike * 7 / 10, cx + spike * 7 / 10, cy + spike * 7 / 10, 2, ink);
    renderer.drawLine(cx - spike * 7 / 10, cy + spike * 7 / 10, cx + spike * 7 / 10, cy - spike * 7 / 10, 2, ink);
    fillCircle(cx - r / 3, cy - r / 3, r / 4 > 1 ? r / 4 : 2, inverted);
}

void MinesweeperActivity::drawBrackets(int x, int y) const {
    constexpr int L = 10;
    constexpr int T = 3;
    const int x2 = x + cellS_ - 1;
    const int y2 = y + cellS_ - 1;
    renderer.fillRect(x + 1, y + 1, L, T, true);
    renderer.fillRect(x + 1, y + 1, T, L, true);
    renderer.fillRect(x2 - L, y + 1, L, T, true);
    renderer.fillRect(x2 - T, y + 1, T, L, true);
    renderer.fillRect(x + 1, y2 - T, L, T, true);
    renderer.fillRect(x + 1, y2 - L, T, L, true);
    renderer.fillRect(x2 - L, y2 - T, L, T, true);
    renderer.fillRect(x2 - T, y2 - L, T, L, true);
}

void MinesweeperActivity::drawCell(int idx) const {
    const int cols = board_.cols();
    const int r = idx / cols;
    const int c = idx % cols;
    const int x = boardLeft_ + c * cellS_;
    const int y = boardTop_ + r * cellS_;
    const int cx = x + cellS_ / 2;
    const int cy = y + cellS_ / 2;
    const bool lost = board_.outcome() == Outcome::LOST;
    const uint8_t st = board_.state(idx);

    if (st == Board::OPEN) {
        if (board_.isMine(idx)) {
            renderer.fillRect(x, y, cellS_, cellS_, true);
            drawMine(cx, cy, cellS_ / 2, true);
        } else if (board_.adjacent(idx) > 0) {
            const int fontId = cellS_ >= 40 ? UI_12_FONT_ID : UI_10_FONT_ID;
            char label[2] = {(char)('0' + board_.adjacent(idx)), 0};
            const int w = renderer.getTextWidth(fontId, label, EpdFontFamily::BOLD);
            const int h = renderer.getLineHeight(fontId);
            renderer.drawText(fontId, cx - w / 2, cy - h / 2, label, true, EpdFontFamily::BOLD);
        }
        return;
    }

    renderer.fillRectDither(x + 2, y + 2, cellS_ - 4, cellS_ - 4, Color::LightGray);

    if (st == Board::FLAG) {
        drawFlag(cx, cy, cellS_ / 2);
        if (lost && !board_.isMine(idx)) {
            const int s = cellS_ / 2 - 2;
            renderer.drawLine(cx - s, cy - s, cx + s, cy + s, 3, true);
            renderer.drawLine(cx - s, cy + s, cx + s, cy - s, 3, true);
        }
    } else if (lost && board_.isMine(idx)) {
        drawMine(cx, cy, cellS_ / 2, false);
    }
}

void MinesweeperActivity::drawBoard() const {
    const int cols = board_.cols();
    const int rows = board_.rows();

    for (int r = 0; r <= rows; r++) {
        renderer.drawLine(boardLeft_, boardTop_ + r * cellS_, boardLeft_ + cols * cellS_, boardTop_ + r * cellS_);
    }
    for (int c = 0; c <= cols; c++) {
        renderer.drawLine(boardLeft_ + c * cellS_, boardTop_, boardLeft_ + c * cellS_, boardTop_ + rows * cellS_);
    }
    renderer.drawRect(boardLeft_ - 1, boardTop_ - 1, cols * cellS_ + 3, rows * cellS_ + 3, 2, true);

    for (int i = 0; i < cols * rows; i++) drawCell(i);

    if (!over()) {
        const int r = cursor_ / cols;
        const int c = cursor_ % cols;
        drawBrackets(boardLeft_ + c * cellS_, boardTop_ + r * cellS_);
    }
}

void MinesweeperActivity::drawModeBar() const {
    const int w = (screenW_ - 48) / 2;
    const int y = barY_;
    for (int i = 0; i < 2; i++) {
        const int x = 16 + i * (w + 16);
        const bool sel = (i == 1) == flagMode_;
        if (sel) {
            renderer.fillRoundedRect(x, y, w, BAR_H, 10, Color::Black);
        } else {
            renderer.drawRoundedRect(x, y, w, BAR_H, 2, 10, true);
        }
        const char* label = i == 0 ? "Dig" : "Flag";
        const int tw = renderer.getTextWidth(UI_12_FONT_ID, label, EpdFontFamily::BOLD);
        const int th = renderer.getLineHeight(UI_12_FONT_ID);
        renderer.drawText(UI_12_FONT_ID, x + (w - tw) / 2, y + (BAR_H - th) / 2, label, !sel, EpdFontFamily::BOLD);
    }
}

void MinesweeperActivity::drawStatus() const {
    char title[40];
    snprintf(title, sizeof(title), "Minesweeper  -  %s", LEVELS[level_].name);
    renderer.drawCenteredText(UI_12_FONT_ID, 16, title, true, EpdFontFamily::BOLD);

    char status[64];
    if (board_.outcome() == Outcome::WON) {
        const unsigned long secs = (endMs_ - startMs_) / 1000;
        snprintf(status, sizeof(status), "Cleared in %lu:%02lu - tap to continue", secs / 60, secs % 60);
    } else if (board_.outcome() == Outcome::LOST) {
        snprintf(status, sizeof(status), "Boom - tap to continue");
    } else {
        snprintf(status, sizeof(status), "Mines left: %d", board_.flagsLeft());
    }
    renderer.drawCenteredText(UI_10_FONT_ID, 56, status);
}

void MinesweeperActivity::drawMenu() const {
    renderer.drawCenteredText(UI_12_FONT_ID, 70, "Minesweeper", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, 120, "Choose a difficulty");
    renderer.drawCenteredText(UI_10_FONT_ID, 146, "Hold a square to place a flag");

    for (int i = 0; i < LEVEL_COUNT; i++) {
        const int y = MENU_TOP + i * MENU_ROW_H;
        const bool sel = i == menuIndex_;
        const int x = 24;
        const int w = screenW_ - 48;
        const int h = MENU_ROW_H - 12;
        if (sel) {
            renderer.fillRoundedRect(x, y, w, h, 10, Color::Black);
        } else {
            renderer.drawRoundedRect(x, y, w, h, 2, 10, true);
        }
        renderer.drawText(UI_12_FONT_ID, x + 20, y + 10, LEVELS[i].name, !sel, EpdFontFamily::BOLD);
        renderer.drawText(UI_10_FONT_ID, x + 20, y + 42, LEVELS[i].hint, !sel);
    }
}

void MinesweeperActivity::render(RenderLock&&) {
    renderer.clearScreen();

    if (screen_ == Screen::MENU) {
        drawMenu();
        GUI.drawButtonHints(renderer, "Exit", "Start", "Up", "Down");
    } else {
        drawStatus();
        drawBoard();
        drawModeBar();
        GUI.drawButtonHints(renderer, "Flag", "Dig", "Left", "Right");
        GUI.drawSideButtonHints(renderer, "Up", "Down");
    }

    renderer.displayBuffer();
}
