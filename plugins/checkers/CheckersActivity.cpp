#include "CheckersActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <math.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

using namespace checkers;

const Level CheckersActivity::LEVELS[CheckersActivity::LEVEL_COUNT] = {
    {"Beginner", "Makes plenty of mistakes", 2, 1200, 120},
    {"Casual", "Plays soft and slips up now and then", 4, 2500, 40},
    {"Intermediate", "Looks several moves ahead", 8, 4000, 0},
    {"Hard", "Thinks as deep as it can", 14, 8000, 0},
};

void CheckersActivity::onEnter() {
    Activity::onEnter();

    screenW_ = renderer.getScreenWidth();
    screenH_ = renderer.getScreenHeight();
    computeLayout();

    randomSeed(millis());
    engine_.reset(new Engine());
    hist_.reset(new Position[MAX_HIST]);

    screen_ = Screen::MODE;
    menuIndex_ = 0;
    requestUpdate();
}

void CheckersActivity::onExit() {
    engine_.reset();
    hist_.reset();
    Activity::onExit();
}

void CheckersActivity::computeLayout() {
    cellS_ = screenW_ / 8;
    boardLeft_ = (screenW_ - cellS_ * 8) / 2;
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int usableH = screenH_ - metrics.buttonHintsHeight;
    int top = (usableH - cellS_ * 8) / 2 + 10;
    if (top < 96) top = 96;
    boardTop_ = top;
}

int CheckersActivity::squareAt(int x, int y) const {
    if (x < boardLeft_ || y < boardTop_) return -1;
    const int f = (x - boardLeft_) / cellS_;
    const int r = (y - boardTop_) / cellS_;
    if (f > 7 || r > 7) return -1;
    return r * 8 + f;
}

bool CheckersActivity::humanToMove() const {
    if (result_ != Result::NONE || thinking_) return false;
    return !vsComputer_ || pos_.side == 0;
}

void CheckersActivity::startGame() {
    startPosition(pos_);
    histCount_ = 0;
    selected_ = -1;
    cursor_ = 41;
    lastFrom_ = -1;
    lastTo_ = -1;
    thinking_ = false;
    result_ = Result::NONE;
    memset(legalDest_, 0, sizeof(legalDest_));
    screen_ = Screen::GAME;
    requestUpdate();
}

void CheckersActivity::pushHistory() {
    if (histCount_ >= MAX_HIST) {
        memmove(hist_.get(), hist_.get() + 2, sizeof(Position) * (MAX_HIST - 2));
        histCount_ -= 2;
    }
    hist_[histCount_++] = pos_;
}

int CheckersActivity::repetitionCount() const {
    int count = 1;
    for (int i = 0; i < histCount_; i++) {
        if (samePosition(hist_[i], pos_)) count++;
    }
    return count;
}

void CheckersActivity::checkEnd() {
    Move moves[Engine::MAXM];
    const int n = engine_->legalMoves(pos_, moves);
    result_ = Result::NONE;
    if (n == 0) {
        result_ = pos_.side == 0 ? Result::BLACK_WINS : Result::WHITE_WINS;
    } else if (pos_.half >= Engine::DRAW_HALF) {
        result_ = Result::DRAW_FIFTY;
    } else if (repetitionCount() >= 3) {
        result_ = Result::DRAW_REPEAT;
    }
    if (result_ != Result::NONE) resultAt_ = millis();
}

void CheckersActivity::selectSquare(int sq) {
    selected_ = sq;
    memset(legalDest_, 0, sizeof(legalDest_));
    Move moves[Engine::MAXM];
    const int n = engine_->legalMoves(pos_, moves);
    for (int i = 0; i < n; i++) {
        if (moves[i].from == sq) legalDest_[moves[i].to] = true;
    }
}

void CheckersActivity::onSquare(int sq) {
    cursor_ = sq;
    if (!humanToMove()) return;

    if (selected_ >= 0 && legalDest_[sq]) {
        playHumanMove(selected_, sq);
        return;
    }

    if (pos_.chain >= 0) {
        requestUpdate();
        return;
    }

    if (Engine::ownPiece(pos_, sq)) {
        selectSquare(sq);
        bool any = false;
        for (int i = 0; i < 64; i++) {
            if (legalDest_[i]) any = true;
        }
        if (!any) selected_ = -1;
    } else {
        selected_ = -1;
        memset(legalDest_, 0, sizeof(legalDest_));
    }
    requestUpdate();
}

void CheckersActivity::applyMove(const Move& m) {
    pushHistory();
    Position next;
    Engine::makeMove(pos_, m, next);
    pos_ = next;
    lastFrom_ = m.from;
    lastTo_ = m.to;
}

void CheckersActivity::playHumanMove(int from, int to) {
    Move moves[Engine::MAXM];
    const int n = engine_->legalMoves(pos_, moves);
    int pick = -1;
    for (int i = 0; i < n; i++) {
        if (moves[i].from == from && moves[i].to == to) {
            pick = i;
            break;
        }
    }
    if (pick < 0) return;

    applyMove(moves[pick]);
    selected_ = -1;
    memset(legalDest_, 0, sizeof(legalDest_));

    checkEnd();
    if (result_ == Result::NONE) {
        if (pos_.chain >= 0) {
            cursor_ = pos_.chain;
            selectSquare(pos_.chain);
        } else if (vsComputer_) {
            thinking_ = true;
        }
    }
    requestUpdate();
}

void CheckersActivity::runEngine() {
    int guard = 0;
    while (result_ == Result::NONE && pos_.side == 1 && guard++ < 16) {
        Move m;
        if (!engine_->findBest(pos_, LEVELS[level_], m)) break;
        applyMove(m);
        checkEnd();
    }
}

void CheckersActivity::undo() {
    int idx = -1;
    for (int i = histCount_ - 1; i >= 0; i--) {
        if (vsComputer_) {
            if (hist_[i].side == 0 && hist_[i].chain < 0) {
                idx = i;
                break;
            }
        } else {
            idx = i;
            break;
        }
    }
    if (idx < 0) return;
    pos_ = hist_[idx];
    histCount_ = idx;
    lastFrom_ = -1;
    lastTo_ = -1;
    selected_ = -1;
    memset(legalDest_, 0, sizeof(legalDest_));
    result_ = Result::NONE;
    if (pos_.chain >= 0) {
        cursor_ = pos_.chain;
        selectSquare(pos_.chain);
    }
    requestUpdate();
}

void CheckersActivity::handleMenu() {
    const int count = screen_ == Screen::MODE ? MODE_COUNT : LEVEL_COUNT;

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        if (screen_ == Screen::LEVEL) {
            screen_ = Screen::MODE;
            menuIndex_ = 0;
            requestUpdate();
        } else {
            finish();
        }
        return;
    }

    int tx = 0;
    int ty = 0;
    if (mappedInput.wasScreenTapped(tx, ty)) {
        if (ty >= MENU_TOP) {
            const int row = (ty - MENU_TOP) / MENU_ROW_H;
            if (row >= 0 && row < count) {
                menuIndex_ = row;
                if (screen_ == Screen::MODE) {
                    vsComputer_ = row == 0;
                    if (vsComputer_) {
                        screen_ = Screen::LEVEL;
                        menuIndex_ = 1;
                        requestUpdate();
                    } else {
                        startGame();
                    }
                } else {
                    level_ = row;
                    startGame();
                }
            }
        }
        return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        menuIndex_ = (menuIndex_ + count - 1) % count;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
               mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        menuIndex_ = (menuIndex_ + 1) % count;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
               mappedInput.wasPressed(MappedInputManager::Button::Power)) {
        if (screen_ == Screen::MODE) {
            vsComputer_ = menuIndex_ == 0;
            if (vsComputer_) {
                screen_ = Screen::LEVEL;
                menuIndex_ = 1;
                requestUpdate();
            } else {
                startGame();
            }
        } else {
            level_ = menuIndex_;
            startGame();
        }
    }
}

void CheckersActivity::handleGame() {
    int tx = 0;
    int ty = 0;
    const bool tapped = mappedInput.wasScreenTapped(tx, ty);

    if (result_ != Result::NONE) {
        if (millis() - resultAt_ < 1500) return;
        if (tapped || mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
            screen_ = Screen::MODE;
            menuIndex_ = 0;
            requestUpdate();
        } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
            undo();
        }
        return;
    }

    if (tapped) {
        const int sq = squareAt(tx, ty);
        if (sq >= 0) onSquare(sq);
        return;
    }

    const int r = cursor_ >> 3;
    const int f = cursor_ & 7;
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        if (r > 0) cursor_ -= 8;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        if (r < 7) cursor_ += 8;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        if (f > 0) cursor_ -= 1;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        if (f < 7) cursor_ += 1;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        onSquare(cursor_);
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        if (selected_ >= 0 && pos_.chain < 0) {
            selected_ = -1;
            memset(legalDest_, 0, sizeof(legalDest_));
            requestUpdate();
        } else {
            undo();
        }
    }
}

void CheckersActivity::loop() {
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

    if (screen_ != Screen::GAME) {
        handleMenu();
        return;
    }

    if (thinking_) {
        requestUpdateAndWait();
        runEngine();
        thinking_ = false;
        requestUpdate();
        return;
    }

    handleGame();
}

void CheckersActivity::drawBrackets(int x, int y) const {
    constexpr int L = 14;
    constexpr int T = 3;
    const int x2 = x + cellS_ - 1;
    const int y2 = y + cellS_ - 1;
    renderer.fillRect(x + 2, y + 2, L, T, true);
    renderer.fillRect(x + 2, y + 2, T, L, true);
    renderer.fillRect(x2 - L - 1, y + 2, L, T, true);
    renderer.fillRect(x2 - T - 1, y + 2, T, L, true);
    renderer.fillRect(x + 2, y2 - T - 1, L, T, true);
    renderer.fillRect(x + 2, y2 - L - 1, T, L, true);
    renderer.fillRect(x2 - L - 1, y2 - T - 1, L, T, true);
    renderer.fillRect(x2 - T - 1, y2 - L - 1, T, L, true);
}

void CheckersActivity::fillCircle(int cx, int cy, int r, bool black) const {
    if (r < 2) return;
    constexpr int N = 24;
    int xs[N];
    int ys[N];
    for (int i = 0; i < N; i++) {
        const float a = (float)i * 6.2831853f / (float)N;
        xs[i] = cx + (int)lroundf(cosf(a) * (float)r);
        ys[i] = cy + (int)lroundf(sinf(a) * (float)r);
    }
    renderer.fillPolygon(xs, ys, N, black);
}

void CheckersActivity::drawPiece(int piece, int cx, int cy) const {
    const bool white = piece > 0;
    const bool king = piece == 2 || piece == -2;
    const int r = (cellS_ - 12) / 2;

    fillCircle(cx, cy, r, true);
    if (white) {
        fillCircle(cx, cy, r - 3, false);
        fillCircle(cx, cy, r - 9, true);
        fillCircle(cx, cy, r - 12, false);
    } else {
        fillCircle(cx, cy, r - 7, false);
        fillCircle(cx, cy, r - 10, true);
    }

    if (king) {
        const int w = renderer.getTextWidth(UI_12_FONT_ID, "K", EpdFontFamily::BOLD);
        const int h = renderer.getLineHeight(UI_12_FONT_ID);
        renderer.fillRect(cx - w / 2 - 3, cy - h / 2 + 1, w + 6, h - 2, white);
        renderer.drawText(UI_12_FONT_ID, cx - w / 2, cy - h / 2, "K", !white, EpdFontFamily::BOLD);
    }
}

void CheckersActivity::drawBoard() const {
    const int boardPx = cellS_ * 8;
    renderer.drawRect(boardLeft_ - 2, boardTop_ - 2, boardPx + 4, boardPx + 4, 2, true);

    for (int r = 0; r < 8; r++) {
        for (int f = 0; f < 8; f++) {
            if (((r + f) & 1) == 1) {
                renderer.fillRectDither(boardLeft_ + f * cellS_, boardTop_ + r * cellS_, cellS_, cellS_,
                                        Color::LightGray);
            }
        }
    }

    for (int sq = 0; sq < 64; sq++) {
        const int r = sq >> 3;
        const int f = sq & 7;
        const int x = boardLeft_ + f * cellS_;
        const int y = boardTop_ + r * cellS_;

        if (sq == lastFrom_ || sq == lastTo_) {
            renderer.drawRect(x + 3, y + 3, cellS_ - 6, cellS_ - 6, 2, true);
        }
        if (pos_.b[sq] != 0) {
            drawPiece(pos_.b[sq], x + cellS_ / 2, y + cellS_ / 2);
        }
        if (legalDest_[sq]) {
            const int c = cellS_ / 2;
            renderer.fillRect(x + c - 7, y + c - 7, 14, 14, true);
            renderer.fillRect(x + c - 3, y + c - 3, 6, 6, false);
        }
        if (sq == selected_) {
            renderer.drawRect(x + 1, y + 1, cellS_ - 2, cellS_ - 2, 4, true);
        }
        if (sq == cursor_ && result_ == Result::NONE) {
            drawBrackets(x, y);
        }
    }
}

void CheckersActivity::drawStatus() const {
    char title[56];
    if (vsComputer_) {
        snprintf(title, sizeof(title), "Checkers  -  %s", LEVELS[level_].name);
    } else {
        snprintf(title, sizeof(title), "Checkers  -  2 Players");
    }
    renderer.drawCenteredText(UI_12_FONT_ID, boardTop_ - 64, title, true, EpdFontFamily::BOLD);

    const char* status = "";
    switch (result_) {
        case Result::WHITE_WINS:
            status = vsComputer_ ? "You win" : "Player 1 (White) wins";
            break;
        case Result::BLACK_WINS:
            status = vsComputer_ ? "The computer wins" : "Player 2 (Black) wins";
            break;
        case Result::DRAW_FIFTY:  status = "Draw - no progress"; break;
        case Result::DRAW_REPEAT: status = "Draw by repetition"; break;
        default:
            if (thinking_) {
                status = "Thinking...";
            } else if (pos_.chain >= 0) {
                status = vsComputer_ || pos_.side == 0 ? "Keep jumping" : "Player 2: keep jumping";
            } else if (vsComputer_) {
                status = "Your move";
            } else {
                status = pos_.side == 0 ? "Player 1's move (White)" : "Player 2's move (Black)";
            }
            break;
    }
    renderer.drawCenteredText(UI_10_FONT_ID, boardTop_ - 32, status);

    const int below = boardTop_ + cellS_ * 8 + 16;
    if (result_ != Result::NONE) {
        renderer.drawCenteredText(UI_10_FONT_ID, below, "Tap or press Select for the menu");
    } else {
        char counts[48];
        snprintf(counts, sizeof(counts), "White %d    Black %d", Engine::countPieces(pos_, 0),
                 Engine::countPieces(pos_, 1));
        renderer.drawCenteredText(UI_10_FONT_ID, below, counts);
    }
}

void CheckersActivity::drawMenu() const {
    renderer.drawCenteredText(UI_12_FONT_ID, 70, "Checkers", true, EpdFontFamily::BOLD);

    int count = 0;
    if (screen_ == Screen::MODE) {
        renderer.drawCenteredText(UI_10_FONT_ID, 120, "Choose a game mode");
        renderer.drawCenteredText(UI_10_FONT_ID, 146, "Captures are required");
        count = MODE_COUNT;
    } else {
        renderer.drawCenteredText(UI_10_FONT_ID, 120, "Choose a difficulty");
        renderer.drawCenteredText(UI_10_FONT_ID, 146, "You play white");
        count = LEVEL_COUNT;
    }

    static const char* MODE_NAMES[MODE_COUNT] = {"Player vs Computer", "Player vs Player"};
    static const char* MODE_HINTS[MODE_COUNT] = {"Play against the device", "Two people share the screen"};

    for (int i = 0; i < count; i++) {
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
        const char* name = screen_ == Screen::MODE ? MODE_NAMES[i] : LEVELS[i].name;
        const char* hint = screen_ == Screen::MODE ? MODE_HINTS[i] : LEVELS[i].hint;
        renderer.drawText(UI_12_FONT_ID, x + 20, y + 10, name, !sel, EpdFontFamily::BOLD);
        renderer.drawText(UI_10_FONT_ID, x + 20, y + 42, hint, !sel);
    }
}

void CheckersActivity::render(RenderLock&&) {
    renderer.clearScreen();

    if (screen_ != Screen::GAME) {
        drawMenu();
        GUI.drawButtonHints(renderer, screen_ == Screen::MODE ? "Exit" : "Back", "Select", "Up", "Down");
    } else {
        drawStatus();
        drawBoard();
        GUI.drawButtonHints(renderer, "Undo", "Select", "Left", "Right");
        GUI.drawSideButtonHints(renderer, "Up", "Down");
    }

    renderer.displayBuffer();
}
