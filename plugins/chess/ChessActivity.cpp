#include "ChessActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

using namespace chess;

const Level ChessActivity::LEVELS[ChessActivity::LEVEL_COUNT] = {
    {"Beginner", "Makes plenty of mistakes", 1, 1500, 300, false},
    {"Casual", "Plays soft and slips up now and then", 2, 3000, 80, true},
    {"Intermediate", "Looks several moves ahead", 4, 5000, 0, true},
    {"Hard", "Thinks as deep as it can", 6, 9000, 0, true},
};

namespace {

struct Shape {
    int8_t n;
    bool solid;
    int8_t pts[12][2];
};

const Shape BASE_SHAPE = {4, false, {{-32, 30}, {32, 30}, {32, 42}, {-32, 42}}};

const Shape PAWN_SHAPES[] = {
    {8, false, {{14, -24}, {10, -14}, {0, -10}, {-10, -14}, {-14, -24}, {-10, -34}, {0, -38}, {10, -34}}},
    {4, false, {{-10, -12}, {10, -12}, {22, 30}, {-22, 30}}},
};

const Shape ROOK_SHAPES[] = {
    {4, false, {{-26, -14}, {26, -14}, {24, 30}, {-24, 30}}},
    {4, false, {{-28, -38}, {-14, -38}, {-14, -14}, {-28, -14}}},
    {4, false, {{-7, -38}, {7, -38}, {7, -14}, {-7, -14}}},
    {4, false, {{14, -38}, {28, -38}, {28, -14}, {14, -14}}},
};

const Shape KNIGHT_SHAPES[] = {
    {11, false, {{-22, 30}, {24, 30}, {22, 2}, {14, -20}, {9, -39}, {-3, -31}, {-30, -7}, {-26, 3}, {-12, -1}, {-3, 8}, {-19, 30}}},
};

const Shape BISHOP_SHAPES[] = {
    {4, false, {{-9, -6}, {9, -6}, {22, 30}, {-22, 30}}},
    {4, false, {{0, -42}, {16, -22}, {0, -4}, {-16, -22}}},
};

const Shape QUEEN_SHAPES[] = {
    {4, false, {{-14, -6}, {14, -6}, {24, 30}, {-24, 30}}},
    {4, false, {{-30, -26}, {30, -26}, {14, -4}, {-14, -4}}},
    {8, true, {{-18, -34}, {-20, -28}, {-26, -26}, {-32, -28}, {-34, -34}, {-32, -40}, {-26, -42}, {-20, -40}}},
    {8, true, {{8, -40}, {6, -34}, {0, -32}, {-6, -34}, {-8, -40}, {-6, -46}, {0, -48}, {6, -46}}},
    {8, true, {{34, -34}, {32, -28}, {26, -26}, {20, -28}, {18, -34}, {20, -40}, {26, -42}, {32, -40}}},
};

const Shape KING_SHAPES[] = {
    {4, false, {{-14, -6}, {14, -6}, {24, 30}, {-24, 30}}},
    {4, false, {{-22, -22}, {22, -22}, {14, -4}, {-14, -4}}},
    {4, true, {{-5, -46}, {5, -46}, {5, -22}, {-5, -22}}},
    {4, true, {{-14, -38}, {14, -38}, {14, -30}, {-14, -30}}},
};

}  // namespace

void ChessActivity::onEnter() {
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

void ChessActivity::onExit() {
    engine_.reset();
    hist_.reset();
    Activity::onExit();
}

void ChessActivity::computeLayout() {
    cellS_ = screenW_ / 8;
    boardLeft_ = (screenW_ - cellS_ * 8) / 2;
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int usableH = screenH_ - metrics.buttonHintsHeight;
    int top = (usableH - cellS_ * 8) / 2 + 10;
    if (top < 96) top = 96;
    boardTop_ = top;
}

int ChessActivity::squareAt(int x, int y) const {
    if (x < boardLeft_ || y < boardTop_) return -1;
    const int f = (x - boardLeft_) / cellS_;
    const int r = (y - boardTop_) / cellS_;
    if (f > 7 || r > 7) return -1;
    return r * 8 + f;
}

void ChessActivity::startGame(int choice) {
    if (vsComputer_) level_ = choice;
    startPosition(pos_);
    histCount_ = 0;
    selected_ = -1;
    cursor_ = 52;
    lastFrom_ = -1;
    lastTo_ = -1;
    thinking_ = false;
    result_ = Result::NONE;
    memset(legalDest_, 0, sizeof(legalDest_));
    screen_ = Screen::GAME;
    requestUpdate();
}

void ChessActivity::pushHistory() {
    if (histCount_ >= MAX_HIST) {
        memmove(hist_.get(), hist_.get() + 2, sizeof(Position) * (MAX_HIST - 2));
        histCount_ -= 2;
    }
    hist_[histCount_++] = pos_;
}

int ChessActivity::repetitionCount() const {
    int count = 1;
    for (int i = 0; i < histCount_; i++) {
        if (samePosition(hist_[i], pos_)) count++;
    }
    return count;
}

void ChessActivity::checkEnd() {
    Move moves[Engine::MAXM];
    const int n = engine_->legalMoves(pos_, moves, Engine::MAXM);
    result_ = Result::NONE;
    if (n == 0) {
        if (engine_->inCheck(pos_)) {
            result_ = pos_.side == 0 ? Result::BLACK_WINS : Result::WHITE_WINS;
        } else {
            result_ = Result::DRAW_STALEMATE;
        }
    } else if (Engine::insufficientMaterial(pos_)) {
        result_ = Result::DRAW_MATERIAL;
    } else if (pos_.half >= 100) {
        result_ = Result::DRAW_FIFTY;
    } else if (repetitionCount() >= 3) {
        result_ = Result::DRAW_REPEAT;
    }
    if (result_ != Result::NONE) resultAt_ = millis();
}

void ChessActivity::selectSquare(int sq) {
    selected_ = sq;
    memset(legalDest_, 0, sizeof(legalDest_));
    Move moves[Engine::MAXM];
    const int n = engine_->legalMoves(pos_, moves, Engine::MAXM);
    for (int i = 0; i < n; i++) {
        if (moves[i].from == sq) legalDest_[moves[i].to] = true;
    }
}

void ChessActivity::onSquare(int sq) {
    cursor_ = sq;
    if (selected_ >= 0 && legalDest_[sq]) {
        playPlayerMove(selected_, sq);
        return;
    }
    if (pos_.b[sq] != 0 && (pos_.b[sq] > 0) == (pos_.side == 0)) {
        selectSquare(sq);
    } else {
        selected_ = -1;
        memset(legalDest_, 0, sizeof(legalDest_));
    }
    requestUpdate();
}

void ChessActivity::playPlayerMove(int from, int to) {
    Move moves[Engine::MAXM];
    const int n = engine_->legalMoves(pos_, moves, Engine::MAXM);
    int pick = -1;
    for (int i = 0; i < n; i++) {
        if (moves[i].from != from || moves[i].to != to) continue;
        if (moves[i].promo == 0 || moves[i].promo == QUEEN) {
            pick = i;
            break;
        }
    }
    if (pick < 0) return;

    pushHistory();
    Position next;
    Engine::makeMove(pos_, moves[pick], next);
    pos_ = next;
    lastFrom_ = from;
    lastTo_ = to;
    selected_ = -1;
    memset(legalDest_, 0, sizeof(legalDest_));

    checkEnd();
    thinking_ = vsComputer_ && result_ == Result::NONE;
    requestUpdate();
}

void ChessActivity::runEngine() {
    Move m;
    if (engine_->findBest(pos_, LEVELS[level_], m)) {
        pushHistory();
        Position next;
        Engine::makeMove(pos_, m, next);
        pos_ = next;
        lastFrom_ = m.from;
        lastTo_ = m.to;
    }
    checkEnd();
}

void ChessActivity::undo() {
    int idx = -1;
    if (vsComputer_) {
        for (int i = histCount_ - 1; i >= 0; i--) {
            if (hist_[i].side == 0) {
                idx = i;
                break;
            }
        }
    } else {
        idx = histCount_ - 1;
    }
    if (idx < 0) return;
    pos_ = hist_[idx];
    histCount_ = idx;
    lastFrom_ = -1;
    lastTo_ = -1;
    selected_ = -1;
    memset(legalDest_, 0, sizeof(legalDest_));
    result_ = Result::NONE;
    requestUpdate();
}

void ChessActivity::handleMenu() {
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

    const auto choose = [this](int row) {
        menuIndex_ = row;
        if (screen_ == Screen::MODE) {
            vsComputer_ = row == 0;
            if (vsComputer_) {
                screen_ = Screen::LEVEL;
                menuIndex_ = 1;
                requestUpdate();
            } else {
                startGame(0);
            }
        } else {
            startGame(row);
        }
    };

    int tx = 0;
    int ty = 0;
    if (mappedInput.wasScreenTapped(tx, ty)) {
        if (ty >= MENU_TOP) {
            const int row = (ty - MENU_TOP) / MENU_ROW_H;
            if (row >= 0 && row < count) choose(row);
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
        choose(menuIndex_);
    }
}

void ChessActivity::handleGame() {
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
        if (selected_ >= 0) {
            selected_ = -1;
            memset(legalDest_, 0, sizeof(legalDest_));
            requestUpdate();
        } else {
            undo();
        }
    }
}

void ChessActivity::loop() {
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

void ChessActivity::drawBrackets(int x, int y) const {
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

void ChessActivity::drawPiece(int piece, int cx, int cy) const {
    const bool white = piece > 0;
    const int type = white ? piece : -piece;

    const Shape* shapes = nullptr;
    int count = 0;
    switch (type) {
        case PAWN:   shapes = PAWN_SHAPES;   count = 2; break;
        case KNIGHT: shapes = KNIGHT_SHAPES; count = 1; break;
        case BISHOP: shapes = BISHOP_SHAPES; count = 2; break;
        case ROOK:   shapes = ROOK_SHAPES;   count = 4; break;
        case QUEEN:  shapes = QUEEN_SHAPES;  count = 5; break;
        default:     shapes = KING_SHAPES;   count = 4; break;
    }

    const auto drawShape = [&](const Shape& sh) {
        int xs[12];
        int ys[12];
        int sumX = 0;
        int sumY = 0;
        for (int i = 0; i < sh.n; i++) {
            xs[i] = cx + sh.pts[i][0] * cellS_ / 100;
            ys[i] = cy + sh.pts[i][1] * cellS_ / 100;
            sumX += xs[i];
            sumY += ys[i];
        }
        renderer.fillPolygon(xs, ys, sh.n, true);
        if (!white || sh.solid) return;

        const int mx = sumX / sh.n;
        const int my = sumY / sh.n;
        int hx = 0;
        int hy = 0;
        for (int i = 0; i < sh.n; i++) {
            const int dx = xs[i] > mx ? xs[i] - mx : mx - xs[i];
            const int dy = ys[i] > my ? ys[i] - my : my - ys[i];
            if (dx > hx) hx = dx;
            if (dy > hy) hy = dy;
        }
        if (hx <= 3 || hy <= 3) return;
        int ix[12];
        int iy[12];
        for (int i = 0; i < sh.n; i++) {
            ix[i] = mx + (xs[i] - mx) * (hx - 2) / hx;
            iy[i] = my + (ys[i] - my) * (hy - 2) / hy;
        }
        renderer.fillPolygon(ix, iy, sh.n, false);
    };

    drawShape(BASE_SHAPE);
    for (int i = 0; i < count; i++) drawShape(shapes[i]);

    const bool mark = !white;
    if (type == BISHOP) {
        renderer.drawLine(cx - 3 * cellS_ / 100, cy - 12 * cellS_ / 100, cx + 8 * cellS_ / 100,
                          cy - 26 * cellS_ / 100, 2, !mark);
    } else if (type == KNIGHT) {
        const int ex = cx - 6 * cellS_ / 100;
        const int ey = cy - 20 * cellS_ / 100;
        renderer.fillRect(ex - 2, ey - 2, 4, 4, !mark);
    }
}

void ChessActivity::drawBoard() const {
    const int boardPx = cellS_ * 8;
    renderer.drawRect(boardLeft_ - 2, boardTop_ - 2, boardPx + 4, boardPx + 4, 2, true);

    for (int r = 0; r < 8; r++) {
        for (int f = 0; f < 8; f++) {
            const int x = boardLeft_ + f * cellS_;
            const int y = boardTop_ + r * cellS_;
            if (((r + f) & 1) == 1) {
                renderer.fillRectDither(x, y, cellS_, cellS_, Color::LightGray);
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
            drawPiece(pos_.b[sq], x + cellS_ / 2, y + cellS_ / 2 - 2);
        }
        if (legalDest_[sq]) {
            if (pos_.b[sq] == 0) {
                const int c = cellS_ / 2;
                renderer.fillRect(x + c - 6, y + c - 6, 12, 12, true);
                renderer.fillRect(x + c - 3, y + c - 3, 6, 6, false);
            } else {
                renderer.drawRect(x + 2, y + 2, cellS_ - 4, cellS_ - 4, 4, true);
            }
        }
        if (sq == selected_) {
            renderer.drawRect(x + 1, y + 1, cellS_ - 2, cellS_ - 2, 4, true);
        }
        if (sq == cursor_ && result_ == Result::NONE) {
            drawBrackets(x, y);
        }
    }
}

void ChessActivity::drawStatus() const {
    char title[48];
    if (vsComputer_) {
        snprintf(title, sizeof(title), "Chess  -  %s", LEVELS[level_].name);
    } else {
        snprintf(title, sizeof(title), "Chess  -  2 Players");
    }
    renderer.drawCenteredText(UI_12_FONT_ID, boardTop_ - 64, title, true, EpdFontFamily::BOLD);

    const char* status = "Your move";
    switch (result_) {
        case Result::WHITE_WINS:     status = vsComputer_ ? "Checkmate - you win" : "Checkmate - White wins"; break;
        case Result::BLACK_WINS:     status = vsComputer_ ? "Checkmate - you lose" : "Checkmate - Black wins"; break;
        case Result::DRAW_STALEMATE: status = "Draw by stalemate"; break;
        case Result::DRAW_REPEAT:    status = "Draw by repetition"; break;
        case Result::DRAW_MATERIAL:  status = "Draw - not enough pieces"; break;
        case Result::DRAW_FIFTY:     status = "Draw by the 50 move rule"; break;
        default:
            if (thinking_) {
                status = "Thinking...";
            } else if (vsComputer_) {
                status = engine_ && engine_->inCheck(pos_) ? "Your move - check" : "Your move";
            } else {
                const bool check = engine_ && engine_->inCheck(pos_);
                if (pos_.side == 0) {
                    status = check ? "White's move - check" : "White's move";
                } else {
                    status = check ? "Black's move - check" : "Black's move";
                }
            }
            break;
    }
    renderer.drawCenteredText(UI_10_FONT_ID, boardTop_ - 32, status);

    const int below = boardTop_ + cellS_ * 8 + 16;
    if (result_ != Result::NONE) {
        renderer.drawCenteredText(UI_10_FONT_ID, below, "Tap or press Select for the menu");
    } else if (lastFrom_ >= 0) {
        char last[32];
        snprintf(last, sizeof(last), "Last move: %c%c to %c%c", 'a' + (lastFrom_ & 7), '8' - (lastFrom_ >> 3),
                 'a' + (lastTo_ & 7), '8' - (lastTo_ >> 3));
        renderer.drawCenteredText(UI_10_FONT_ID, below, last);
    }
}

void ChessActivity::drawMenu() const {
    renderer.drawCenteredText(UI_12_FONT_ID, 70, "Chess", true, EpdFontFamily::BOLD);

    int count = 0;
    if (screen_ == Screen::MODE) {
        renderer.drawCenteredText(UI_10_FONT_ID, 120, "Choose how many players");
        count = MODE_COUNT;
    } else {
        renderer.drawCenteredText(UI_10_FONT_ID, 120, "Choose a difficulty");
        renderer.drawCenteredText(UI_10_FONT_ID, 146, "You play white");
        count = LEVEL_COUNT;
    }

    static const char* MODE_NAMES[MODE_COUNT] = {"1 Player", "2 Players"};
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

void ChessActivity::render(RenderLock&&) {
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
