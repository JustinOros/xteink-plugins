#include "SudokuActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

using namespace sudoku;

const SudokuActivity::Difficulty SudokuActivity::LEVELS[SudokuActivity::LEVEL_COUNT] = {
    {"Easy", "40 numbers to start with", 40},
    {"Medium", "32 numbers to start with", 32},
    {"Hard", "25 numbers to start with", 25},
};

void SudokuActivity::onEnter() {
    Activity::onEnter();

    screenW_ = renderer.getScreenWidth();
    screenH_ = renderer.getScreenHeight();
    computeLayout();

    randomSeed(millis());
    solver_.reset(new Solver());

    screen_ = Screen::MENU;
    menuIndex_ = 0;
    requestUpdate();
}

void SudokuActivity::onExit() {
    solver_.reset();
    Activity::onExit();
}

void SudokuActivity::computeLayout() {
    cellS_ = screenW_ / 9;
    boardLeft_ = (screenW_ - cellS_ * 9) / 2;
    padTop_ = BOARD_TOP + cellS_ * 9 + 14;
    padH_ = cellS_ + 4;
    actTop_ = padTop_ + padH_ + 10;
    actH_ = 52;
}

void SudokuActivity::generate() {
    uint8_t puzzle[81];
    generatePuzzle(LEVELS[level_].givens, 6000, puzzle, solution_, *solver_);
    memcpy(grid_, puzzle, 81);
    for (int i = 0; i < 81; i++) {
        given_[i] = puzzle[i] != 0;
        notes_[i] = 0;
    }
    undoCount_ = 0;
    cursor_ = 40;
    notesMode_ = false;
    solved_ = false;
    hints_ = 0;
    startMs_ = millis();
    endMs_ = 0;
}

void SudokuActivity::pushUndo(int cell) {
    if (undoCount_ >= MAX_UNDO) {
        memmove(undo_, undo_ + 1, sizeof(UndoEntry) * (MAX_UNDO - 1));
        undoCount_--;
    }
    undo_[undoCount_].cell = (uint8_t)cell;
    undo_[undoCount_].value = grid_[cell];
    undo_[undoCount_].notes = notes_[cell];
    undoCount_++;
}

bool SudokuActivity::conflict(int cell) const {
    const uint8_t v = grid_[cell];
    if (v == 0) return false;
    const int r = cell / 9;
    const int c = cell % 9;
    for (int i = 0; i < 9; i++) {
        if (i != c && grid_[r * 9 + i] == v) return true;
        if (i != r && grid_[i * 9 + c] == v) return true;
    }
    const int br = (r / 3) * 3;
    const int bc = (c / 3) * 3;
    for (int dr = 0; dr < 3; dr++) {
        for (int dc = 0; dc < 3; dc++) {
            const int idx = (br + dr) * 9 + bc + dc;
            if (idx != cell && grid_[idx] == v) return true;
        }
    }
    return false;
}

int SudokuActivity::digitCount(int d) const {
    int n = 0;
    for (int i = 0; i < 81; i++) {
        if (grid_[i] == d) n++;
    }
    return n;
}

void SudokuActivity::checkSolved() {
    for (int i = 0; i < 81; i++) {
        if (grid_[i] != solution_[i]) return;
    }
    solved_ = true;
    endMs_ = millis();
    solvedAt_ = millis();
}

void SudokuActivity::setDigit(int d) {
    if (solved_ || given_[cursor_]) return;
    pushUndo(cursor_);
    if (notesMode_) {
        if (grid_[cursor_] == 0) notes_[cursor_] ^= (uint16_t)(1u << (d - 1));
    } else {
        if (grid_[cursor_] == d) {
            grid_[cursor_] = 0;
        } else {
            grid_[cursor_] = (uint8_t)d;
            notes_[cursor_] = 0;
        }
        checkSolved();
    }
    requestUpdate();
}

void SudokuActivity::eraseCell() {
    if (solved_ || given_[cursor_]) return;
    if (grid_[cursor_] == 0 && notes_[cursor_] == 0) return;
    pushUndo(cursor_);
    grid_[cursor_] = 0;
    notes_[cursor_] = 0;
    requestUpdate();
}

void SudokuActivity::undo() {
    if (solved_ || undoCount_ == 0) return;
    undoCount_--;
    const UndoEntry& e = undo_[undoCount_];
    grid_[e.cell] = e.value;
    notes_[e.cell] = e.notes;
    cursor_ = e.cell;
    requestUpdate();
}

void SudokuActivity::hint() {
    if (solved_ || given_[cursor_]) return;
    if (grid_[cursor_] == solution_[cursor_]) return;
    pushUndo(cursor_);
    grid_[cursor_] = solution_[cursor_];
    notes_[cursor_] = 0;
    hints_++;
    checkSolved();
    requestUpdate();
}

int SudokuActivity::cellAt(int x, int y) const {
    if (x < boardLeft_ || y < BOARD_TOP) return -1;
    const int c = (x - boardLeft_) / cellS_;
    const int r = (y - BOARD_TOP) / cellS_;
    if (c > 8 || r > 8) return -1;
    return r * 9 + c;
}

void SudokuActivity::handleTap(int x, int y) {
    const int idx = cellAt(x, y);
    if (idx >= 0) {
        cursor_ = idx;
        requestUpdate();
        return;
    }

    if (y >= padTop_ && y < padTop_ + padH_) {
        const int d = (x - boardLeft_) / cellS_ + 1;
        if (d >= 1 && d <= 9) setDigit(d);
        return;
    }

    if (y >= actTop_ && y < actTop_ + actH_) {
        const int w = (screenW_ - 16) / 4;
        const int col = (x - 8) / w;
        switch (col) {
            case 0: notesMode_ = !notesMode_; requestUpdate(); break;
            case 1: eraseCell(); break;
            case 2: undo(); break;
            case 3: hint(); break;
            default: break;
        }
    }
}

void SudokuActivity::handleMenu() {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        finish();
        return;
    }

    int tx = 0;
    int ty = 0;
    const auto start = [this](int row) {
        level_ = row;
        generating_ = true;
        requestUpdate();
    };

    if (mappedInput.wasScreenTapped(tx, ty)) {
        if (ty >= MENU_TOP) {
            const int row = (ty - MENU_TOP) / MENU_ROW_H;
            if (row >= 0 && row < LEVEL_COUNT) {
                menuIndex_ = row;
                start(row);
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
        start(menuIndex_);
    }
}

void SudokuActivity::handleGame() {
    int tx = 0;
    int ty = 0;

    if (mappedInput.wasScreenTapped(tx, ty)) {
        if (solved_) {
            if (millis() - solvedAt_ > 1000) {
                screen_ = Screen::MENU;
                menuIndex_ = level_;
                requestUpdate();
            }
            return;
        }
        handleTap(tx, ty);
        return;
    }

    if (solved_) {
        if (millis() - solvedAt_ > 1000 && (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
                                            mappedInput.wasPressed(MappedInputManager::Button::Back))) {
            screen_ = Screen::MENU;
            menuIndex_ = level_;
            requestUpdate();
        }
        return;
    }

    const int r = cursor_ / 9;
    const int c = cursor_ % 9;
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        if (r > 0) cursor_ -= 9;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        if (r < 8) cursor_ += 9;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        if (c > 0) cursor_ -= 1;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        if (c < 8) cursor_ += 1;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        if (!given_[cursor_]) {
            const int next = (grid_[cursor_] + 1) % 10;
            if (next == 0) {
                eraseCell();
            } else {
                const bool wasNotes = notesMode_;
                notesMode_ = false;
                pushUndo(cursor_);
                grid_[cursor_] = (uint8_t)next;
                notes_[cursor_] = 0;
                notesMode_ = wasNotes;
                checkSolved();
                requestUpdate();
            }
        }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        eraseCell();
    }
}

void SudokuActivity::loop() {
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

    if (generating_) {
        requestUpdateAndWait();
        generate();
        generating_ = false;
        screen_ = Screen::GAME;
        requestUpdate();
        return;
    }

    if (screen_ == Screen::MENU) {
        handleMenu();
    } else {
        handleGame();
    }
}

void SudokuActivity::drawButton(int x, int y, int w, int h, const char* label, bool selected, bool dim) const {
    if (selected) {
        renderer.fillRoundedRect(x, y, w, h, 8, Color::Black);
    } else {
        if (dim) renderer.fillRectDither(x + 2, y + 2, w - 4, h - 4, Color::LightGray);
        renderer.drawRoundedRect(x, y, w, h, 2, 8, true);
    }
    const int fontId = UI_10_FONT_ID;
    const int tw = renderer.getTextWidth(fontId, label, EpdFontFamily::BOLD);
    const int th = renderer.getLineHeight(fontId);
    renderer.drawText(fontId, x + (w - tw) / 2, y + (h - th) / 2, label, !selected, EpdFontFamily::BOLD);
}

void SudokuActivity::drawBoard() const {
    const int size = cellS_ * 9;
    const int selR = cursor_ / 9;
    const int selC = cursor_ % 9;

    for (int i = 0; i < 81; i++) {
        const int r = i / 9;
        const int c = i % 9;
        const bool peer = r == selR || c == selC || ((r / 3 == selR / 3) && (c / 3 == selC / 3));
        if (peer && i != cursor_ && !solved_) {
            renderer.fillRectDither(boardLeft_ + c * cellS_ + 1, BOARD_TOP + r * cellS_ + 1, cellS_ - 1, cellS_ - 1,
                                    Color::LightGray);
        }
    }

    for (int k = 0; k <= 9; k++) {
        const int thick = (k % 3 == 0) ? 3 : 1;
        renderer.fillRect(boardLeft_, BOARD_TOP + k * cellS_ - thick / 2, size + 1, thick, true);
        renderer.fillRect(boardLeft_ + k * cellS_ - thick / 2, BOARD_TOP, thick, size + 1, true);
    }

    const int fontId = UI_12_FONT_ID;
    const int lineH = renderer.getLineHeight(fontId);
    for (int i = 0; i < 81; i++) {
        const int r = i / 9;
        const int c = i % 9;
        const int x = boardLeft_ + c * cellS_;
        const int y = BOARD_TOP + r * cellS_;

        if (i == cursor_) {
            renderer.fillRect(x + 2, y + 2, cellS_ - 3, cellS_ - 3, false);
        }

        if (grid_[i] != 0) {
            char label[2] = {(char)('0' + grid_[i]), 0};
            const int tw = renderer.getTextWidth(fontId, label, EpdFontFamily::BOLD);
            const bool bad = conflict(i);
            if (bad) renderer.fillRect(x + 4, y + 4, cellS_ - 7, cellS_ - 7, true);
            renderer.drawText(fontId, x + (cellS_ - tw) / 2, y + (cellS_ - lineH) / 2, label, !bad,
                              given_[i] ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
        } else if (notes_[i] != 0) {
            const int sub = cellS_ / 3;
            for (int d = 0; d < 9; d++) {
                if (!(notes_[i] & (1u << d))) continue;
                char label[2] = {(char)('1' + d), 0};
                const int nx = x + (d % 3) * sub + sub / 2;
                const int ny = y + (d / 3) * sub + sub / 2;
                const int tw = renderer.getTextWidth(SMALL_FONT_ID, label);
                const int th = renderer.getLineHeight(SMALL_FONT_ID);
                renderer.drawText(SMALL_FONT_ID, nx - tw / 2, ny - th / 2, label, true);
            }
        }

        if (i == cursor_) {
            renderer.drawRect(x + 2, y + 2, cellS_ - 3, cellS_ - 3, 3, true);
        }
    }
}

void SudokuActivity::drawPad() const {
    for (int d = 1; d <= 9; d++) {
        const int x = boardLeft_ + (d - 1) * cellS_;
        char label[2] = {(char)('0' + d), 0};
        drawButton(x + 2, padTop_, cellS_ - 4, padH_, label, false, digitCount(d) >= 9);
    }

    const int w = (screenW_ - 16) / 4;
    drawButton(8 + 0 * w + 2, actTop_, w - 4, actH_, "Notes", notesMode_, false);
    drawButton(8 + 1 * w + 2, actTop_, w - 4, actH_, "Erase", false, false);
    drawButton(8 + 2 * w + 2, actTop_, w - 4, actH_, "Undo", false, undoCount_ == 0);
    drawButton(8 + 3 * w + 2, actTop_, w - 4, actH_, "Hint", false, false);
}

void SudokuActivity::drawStatus() const {
    char title[40];
    snprintf(title, sizeof(title), "Sudoku  -  %s", LEVELS[level_].name);
    renderer.drawCenteredText(UI_12_FONT_ID, 14, title, true, EpdFontFamily::BOLD);

    char status[64];
    if (solved_) {
        const unsigned long secs = (endMs_ - startMs_) / 1000;
        snprintf(status, sizeof(status), "Solved in %lu:%02lu, %d hints - tap to continue", secs / 60, secs % 60,
                 hints_);
    } else {
        snprintf(status, sizeof(status), "Black squares show repeated numbers");
    }
    renderer.drawCenteredText(UI_10_FONT_ID, 50, status);
}

void SudokuActivity::drawMenu() const {
    renderer.drawCenteredText(UI_12_FONT_ID, 70, "Sudoku", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, 120, "Choose a difficulty");
    renderer.drawCenteredText(UI_10_FONT_ID, 146, "Every puzzle has exactly one solution");

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

void SudokuActivity::render(RenderLock&&) {
    renderer.clearScreen();

    if (generating_) {
        renderer.drawCenteredText(UI_12_FONT_ID, screenH_ / 2 - 20, "Making a puzzle...", true, EpdFontFamily::BOLD);
    } else if (screen_ == Screen::MENU) {
        drawMenu();
        GUI.drawButtonHints(renderer, "Exit", "Start", "Up", "Down");
    } else {
        drawStatus();
        drawBoard();
        drawPad();
        GUI.drawButtonHints(renderer, "Erase", "Next digit", "Left", "Right");
        GUI.drawSideButtonHints(renderer, "Up", "Down");
    }

    renderer.displayBuffer();
}
