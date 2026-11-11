#include "SolitaireActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <math.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

using namespace solitaire;

namespace {
const char* RANKS[13] = {"A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
}

void SolitaireActivity::onEnter() {
    Activity::onEnter();

    screenW_ = renderer.getScreenWidth();
    screenH_ = renderer.getScreenHeight();
    computeLayout();

    randomSeed(millis());
    game_.reset(new Game());

    screen_ = Screen::MENU;
    menuIndex_ = 0;
    requestUpdate();
}

void SolitaireActivity::onExit() {
    game_.reset();
    Activity::onExit();
}

void SolitaireActivity::computeLayout() {
    cardW_ = (screenW_ - 8 - 6 * GAP) / 7;
    cardH_ = cardW_ * 14 / 10;
    tabTop_ = TOP_ROW_Y + cardH_ + 12;
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int usableH = screenH_ - metrics.buttonHintsHeight;
    barY_ = usableH - BAR_H - 6;
}

void SolitaireActivity::startGame(int draw) {
    drawCount_ = draw;
    game_->deal(draw);
    clearSelection();
    cursorRow_ = 1;
    cursorCol_ = 0;
    clampCursor();
    won_ = false;
    startMs_ = millis();
    endMs_ = 0;
    newArmedAt_ = 0;
    screen_ = Screen::GAME;
    requestUpdate();
}

int SolitaireActivity::upStep(int col) const {
    const State& s = game_->s;
    int down = 0;
    int up = 0;
    for (int i = 0; i < s.tabN[col]; i++) {
        if (s.tab[col][i] & UP) {
            up++;
        } else {
            down++;
        }
    }
    constexpr int DOWN_STEP = 14;
    int step = 32;
    if (up > 1) {
        const int avail = barY_ - 6 - tabTop_ - cardH_ - down * DOWN_STEP;
        const int fit = avail / (up - 1);
        if (fit < step) step = fit;
    }
    if (step < 16) step = 16;
    return step;
}

int SolitaireActivity::cardY(int col, int idx) const {
    const State& s = game_->s;
    constexpr int DOWN_STEP = 14;
    const int step = upStep(col);
    int y = tabTop_;
    for (int i = 0; i < idx && i < s.tabN[col]; i++) {
        y += (s.tab[col][i] & UP) ? step : DOWN_STEP;
    }
    return y;
}

SolitaireActivity::Target SolitaireActivity::hitTest(int x, int y) const {
    int col = (x - 4) / (cardW_ + GAP);
    if (col < 0 || col > 6) return {Kind::NONE, 0, 0};

    if (y >= TOP_ROW_Y && y < TOP_ROW_Y + cardH_) {
        if (col == 0) return {Kind::STOCK, 0, 0};
        if (col == 1) return {Kind::WASTE, 0, 0};
        if (col >= 3) return {Kind::FOUND, col - 3, 0};
        return {Kind::NONE, 0, 0};
    }

    if (y >= tabTop_ && y < barY_) {
        const State& s = game_->s;
        const int n = s.tabN[col];
        if (n == 0) return {Kind::TAB, col, -1};
        int idx = 0;
        for (int i = 0; i < n; i++) {
            if (cardY(col, i) <= y) idx = i;
        }
        return {Kind::TAB, col, idx};
    }
    return {Kind::NONE, 0, 0};
}

void SolitaireActivity::clampCursor() {
    if (cursorRow_ == 0) {
        if (cursorCol_ == 2) cursorCol_ = 1;
        return;
    }
    const int n = game_->s.tabN[cursorCol_];
    if (n == 0) {
        cursorIdx_ = -1;
        return;
    }
    if (cursorIdx_ < 0 || cursorIdx_ >= n) cursorIdx_ = n - 1;
    int first = n - 1;
    while (first > 0 && (game_->s.tab[cursorCol_][first - 1] & UP)) first--;
    if (cursorIdx_ < first) cursorIdx_ = first;
}

SolitaireActivity::Target SolitaireActivity::cursorTarget() const {
    if (cursorRow_ == 0) {
        if (cursorCol_ == 0) return {Kind::STOCK, 0, 0};
        if (cursorCol_ == 1) return {Kind::WASTE, 0, 0};
        if (cursorCol_ >= 3) return {Kind::FOUND, cursorCol_ - 3, 0};
        return {Kind::NONE, 0, 0};
    }
    return {Kind::TAB, cursorCol_, cursorIdx_};
}

bool SolitaireActivity::selectionIs(const Target& t) const {
    switch (selSrc_) {
        case Src::WASTE: return t.kind == Kind::WASTE;
        case Src::FOUND: return t.kind == Kind::FOUND && t.a == selA_;
        case Src::TAB:   return t.kind == Kind::TAB && t.a == selA_ && t.idx == selIdx_;
        default:         return false;
    }
}

void SolitaireActivity::selectTarget(const Target& t) {
    const State& s = game_->s;
    switch (t.kind) {
        case Kind::WASTE:
            if (s.wasteN > 0) { selSrc_ = Src::WASTE; selA_ = 0; selIdx_ = 0; }
            break;
        case Kind::FOUND:
            if (s.found[t.a] > 0) { selSrc_ = Src::FOUND; selA_ = t.a; selIdx_ = 0; }
            break;
        case Kind::TAB:
            if (game_->srcFirst(Src::TAB, t.a, t.idx) >= 0) { selSrc_ = Src::TAB; selA_ = t.a; selIdx_ = t.idx; }
            break;
        default:
            break;
    }
}

void SolitaireActivity::afterMove() {
    clearSelection();
    clampCursor();
    if (game_->won() && !won_) {
        won_ = true;
        endMs_ = millis();
        wonAt_ = millis();
    }
    requestUpdate();
}

void SolitaireActivity::activate(const Target& t) {
    if (won_) return;

    if (t.kind == Kind::STOCK) {
        clearSelection();
        game_->draw();
        afterMove();
        return;
    }
    if (t.kind == Kind::NONE) {
        clearSelection();
        requestUpdate();
        return;
    }

    if (selSrc_ == Src::NONE) {
        selectTarget(t);
        requestUpdate();
        return;
    }

    if (selectionIs(t)) {
        if (game_->smartMove(selSrc_, selA_, selIdx_)) {
            afterMove();
        }
        return;
    }

    bool moved = false;
    if (t.kind == Kind::FOUND) {
        moved = game_->moveToFound(selSrc_, selA_, selIdx_);
    } else if (t.kind == Kind::TAB) {
        moved = game_->moveToTab(selSrc_, selA_, selIdx_, t.a);
    }
    if (moved) {
        afterMove();
        return;
    }

    clearSelection();
    selectTarget(t);
    requestUpdate();
}

void SolitaireActivity::handleBar(int x) {
    const int w = (screenW_ - 16) / 3;
    const int col = (x - 8) / w;
    if (col == 0) {
        game_->undo();
        clearSelection();
        clampCursor();
        newArmedAt_ = 0;
        requestUpdate();
    } else if (col == 1) {
        if (game_->autoAll() > 0) afterMove();
        newArmedAt_ = 0;
    } else if (col == 2) {
        if (newArmedAt_ != 0 && millis() - newArmedAt_ < 4000) {
            startGame(drawCount_);
        } else {
            newArmedAt_ = millis();
            requestUpdate();
        }
    }
}

void SolitaireActivity::handleMenu() {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        finish();
        return;
    }

    int tx = 0;
    int ty = 0;
    if (mappedInput.wasScreenTapped(tx, ty)) {
        if (ty >= MENU_TOP) {
            const int row = (ty - MENU_TOP) / MENU_ROW_H;
            if (row >= 0 && row < MENU_COUNT) {
                menuIndex_ = row;
                startGame(row == 0 ? 1 : 3);
            }
        }
        return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        menuIndex_ = (menuIndex_ + MENU_COUNT - 1) % MENU_COUNT;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
               mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        menuIndex_ = (menuIndex_ + 1) % MENU_COUNT;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
               mappedInput.wasPressed(MappedInputManager::Button::Power)) {
        startGame(menuIndex_ == 0 ? 1 : 3);
    }
}

void SolitaireActivity::handleGame() {
    int tx = 0;
    int ty = 0;

    if (mappedInput.wasScreenTapped(tx, ty)) {
        if (won_) {
            if (millis() - wonAt_ > 1000) {
                screen_ = Screen::MENU;
                menuIndex_ = drawCount_ == 1 ? 0 : 1;
                requestUpdate();
            }
            return;
        }
        if (ty >= barY_ && ty < barY_ + BAR_H) {
            handleBar(tx);
            return;
        }
        newArmedAt_ = 0;
        const Target t = hitTest(tx, ty);
        if (t.kind == Kind::TAB) {
            cursorRow_ = 1;
            cursorCol_ = t.a;
            cursorIdx_ = t.idx;
        } else if (t.kind != Kind::NONE) {
            cursorRow_ = 0;
            cursorCol_ = t.kind == Kind::STOCK ? 0 : (t.kind == Kind::WASTE ? 1 : 3 + t.a);
        }
        activate(t);
        return;
    }

    if (won_) {
        if (millis() - wonAt_ > 1000 && (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
                                         mappedInput.wasPressed(MappedInputManager::Button::Back))) {
            screen_ = Screen::MENU;
            menuIndex_ = drawCount_ == 1 ? 0 : 1;
            requestUpdate();
        }
        return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        cursorCol_ = (cursorCol_ + 6) % 7;
        if (cursorRow_ == 0 && cursorCol_ == 2) cursorCol_ = 1;
        cursorIdx_ = 1000;
        clampCursor();
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        cursorCol_ = (cursorCol_ + 1) % 7;
        if (cursorRow_ == 0 && cursorCol_ == 2) cursorCol_ = 3;
        cursorIdx_ = 1000;
        clampCursor();
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        if (cursorRow_ == 1) {
            const int n = game_->s.tabN[cursorCol_];
            int first = n - 1;
            while (first > 0 && (game_->s.tab[cursorCol_][first - 1] & UP)) first--;
            if (n > 0 && cursorIdx_ > first) {
                cursorIdx_--;
            } else {
                cursorRow_ = 0;
                if (cursorCol_ == 2) cursorCol_ = 1;
            }
        }
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        if (cursorRow_ == 0) {
            cursorRow_ = 1;
            cursorIdx_ = 1000;
        } else {
            cursorIdx_++;
        }
        clampCursor();
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        activate(cursorTarget());
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        if (selSrc_ != Src::NONE) {
            clearSelection();
        } else {
            game_->undo();
            clampCursor();
        }
        requestUpdate();
    }
}

void SolitaireActivity::loop() {
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

void SolitaireActivity::fillCircle(int cx, int cy, int r, bool black) const {
    if (r < 2) return;
    constexpr int N = 16;
    int xs[N];
    int ys[N];
    for (int i = 0; i < N; i++) {
        const float a = (float)i * 6.2831853f / (float)N;
        xs[i] = cx + (int)lroundf(cosf(a) * (float)r);
        ys[i] = cy + (int)lroundf(sinf(a) * (float)r);
    }
    renderer.fillPolygon(xs, ys, N, black);
}

void SolitaireActivity::drawSuit(int suit, int cx, int cy, int s) const {
    const int h = s / 2;
    switch (suit) {
        case 0: {
            int xs[3] = {cx, cx - h, cx + h};
            int ys[3] = {cy - h, cy + h / 5, cy + h / 5};
            renderer.fillPolygon(xs, ys, 3, true);
            fillCircle(cx - h / 2, cy + h / 4, h * 55 / 100, true);
            fillCircle(cx + h / 2, cy + h / 4, h * 55 / 100, true);
            int sx[3] = {cx - h * 3 / 10, cx + h * 3 / 10, cx};
            int sy[3] = {cy + h, cy + h, cy + h / 5};
            renderer.fillPolygon(sx, sy, 3, true);
            break;
        }
        case 1: {
            fillCircle(cx - h / 2, cy - h / 4, h * 55 / 100, true);
            fillCircle(cx + h / 2, cy - h / 4, h * 55 / 100, true);
            int xs[3] = {cx - h, cx + h, cx};
            int ys[3] = {cy - h / 8, cy - h / 8, cy + h};
            renderer.fillPolygon(xs, ys, 3, true);
            break;
        }
        case 2: {
            int xs[4] = {cx, cx + h * 3 / 4, cx, cx - h * 3 / 4};
            int ys[4] = {cy - h, cy, cy + h, cy};
            renderer.fillPolygon(xs, ys, 4, true);
            break;
        }
        default: {
            fillCircle(cx, cy - h / 2, h * 48 / 100, true);
            fillCircle(cx - h / 2, cy + h / 5, h * 48 / 100, true);
            fillCircle(cx + h / 2, cy + h / 5, h * 48 / 100, true);
            int sx[3] = {cx - h * 3 / 10, cx + h * 3 / 10, cx};
            int sy[3] = {cy + h, cy + h, cy};
            renderer.fillPolygon(sx, sy, 3, true);
            break;
        }
    }
}

void SolitaireActivity::drawFrame(int x, int y, int w, int h, int thick) const {
    renderer.drawRect(x, y, w, h, thick, true);
}

void SolitaireActivity::drawBack(int x, int y) const {
    renderer.fillRect(x, y, cardW_, cardH_, false);
    renderer.fillRectDither(x + 3, y + 3, cardW_ - 6, cardH_ - 6, Color::DarkGray);
    renderer.drawRoundedRect(x, y, cardW_, cardH_, 2, 6, true);
}

void SolitaireActivity::drawEmpty(int x, int y) const {
    renderer.drawRoundedRect(x, y, cardW_, cardH_, 2, 6, true);
}

void SolitaireActivity::drawCard(int x, int y, int id, bool fullHeight) const {
    const int suit = suitOf(id);
    renderer.fillRect(x, y, cardW_, cardH_, false);
    if (isRed(id)) renderer.fillRectDither(x + 2, y + 2, cardW_ - 4, cardH_ - 4, Color::LightGray);
    renderer.drawRoundedRect(x, y, cardW_, cardH_, 2, 6, true);

    renderer.drawText(UI_10_FONT_ID, x + 5, y + 3, RANKS[rankOf(id)], true, EpdFontFamily::BOLD);
    drawSuit(suit, x + cardW_ - 14, y + 15, 16);
    if (fullHeight) {
        drawSuit(suit, x + cardW_ / 2, y + cardH_ / 2 + 10, cardW_ * 5 / 10);
    }
}

void SolitaireActivity::drawTopRow() const {
    const State& s = game_->s;

    if (s.stockN > 0) {
        drawBack(colX(0), TOP_ROW_Y);
    } else {
        drawEmpty(colX(0), TOP_ROW_Y);
        if (s.wasteN > 0) {
            const int cx = colX(0) + cardW_ / 2;
            const int cy = TOP_ROW_Y + cardH_ / 2;
            fillCircle(cx, cy, 14, true);
            fillCircle(cx, cy, 9, false);
        }
    }

    if (s.wasteN > 0) {
        drawCard(colX(1), TOP_ROW_Y, s.waste[s.wasteN - 1], true);
    } else {
        drawEmpty(colX(1), TOP_ROW_Y);
    }

    for (int k = 0; k < 4; k++) {
        const int x = colX(3 + k);
        if (s.found[k] > 0) {
            drawCard(x, TOP_ROW_Y, k * 13 + s.found[k] - 1, true);
        } else {
            drawEmpty(x, TOP_ROW_Y);
            drawSuit(k, x + cardW_ / 2, TOP_ROW_Y + cardH_ / 2, cardW_ * 4 / 10);
        }
    }
}

void SolitaireActivity::drawTableau() const {
    const State& s = game_->s;
    for (int c = 0; c < COLS; c++) {
        const int x = colX(c);
        const int n = s.tabN[c];
        if (n == 0) {
            drawEmpty(x, tabTop_);
            continue;
        }
        for (int i = 0; i < n; i++) {
            const int y = cardY(c, i);
            const uint8_t v = s.tab[c][i];
            if (v & UP) {
                drawCard(x, y, v & 0x3F, i == n - 1);
            } else {
                drawBack(x, y);
            }
        }
    }
}

void SolitaireActivity::drawBar() const {
    const int w = (screenW_ - 16) / 3;
    const bool armed = newArmedAt_ != 0 && millis() - newArmedAt_ < 4000;
    const char* labels[3] = {"Undo", "Auto", armed ? "Sure?" : "New"};
    for (int i = 0; i < 3; i++) {
        const int x = 8 + i * w + 2;
        const bool sel = i == 2 && armed;
        if (sel) {
            renderer.fillRoundedRect(x, barY_, w - 4, BAR_H, 8, Color::Black);
        } else {
            if (i == 0 && !game_->canUndo()) {
                renderer.fillRectDither(x + 2, barY_ + 2, w - 8, BAR_H - 4, Color::LightGray);
            }
            renderer.drawRoundedRect(x, barY_, w - 4, BAR_H, 2, 8, true);
        }
        const int tw = renderer.getTextWidth(UI_10_FONT_ID, labels[i], EpdFontFamily::BOLD);
        const int th = renderer.getLineHeight(UI_10_FONT_ID);
        renderer.drawText(UI_10_FONT_ID, x + (w - 4 - tw) / 2, barY_ + (BAR_H - th) / 2, labels[i], !sel,
                          EpdFontFamily::BOLD);
    }
}

void SolitaireActivity::drawStatus() const {
    char title[48];
    snprintf(title, sizeof(title), "Solitaire  -  Draw %d", drawCount_);
    renderer.drawCenteredText(UI_12_FONT_ID, 8, title, true, EpdFontFamily::BOLD);

    char status[64];
    if (won_) {
        const unsigned long secs = (endMs_ - startMs_) / 1000;
        snprintf(status, sizeof(status), "You won in %lu:%02lu, %u moves - tap to continue", secs / 60, secs % 60,
                 (unsigned)game_->s.moves);
    } else {
        snprintf(status, sizeof(status), "Moves: %u", (unsigned)game_->s.moves);
    }
    renderer.drawCenteredText(UI_10_FONT_ID, 42, status);
}

void SolitaireActivity::drawMenu() const {
    renderer.drawCenteredText(UI_12_FONT_ID, 70, "Solitaire", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, 120, "Choose how to draw from the deck");
    renderer.drawCenteredText(UI_10_FONT_ID, 146, "Tap a card twice to send it to the best spot");

    static const char* NAMES[MENU_COUNT] = {"Draw One", "Draw Three"};
    static const char* HINTS[MENU_COUNT] = {"Turn over one card at a time", "Turn over three cards at a time"};
    for (int i = 0; i < MENU_COUNT; i++) {
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
        renderer.drawText(UI_12_FONT_ID, x + 20, y + 10, NAMES[i], !sel, EpdFontFamily::BOLD);
        renderer.drawText(UI_10_FONT_ID, x + 20, y + 42, HINTS[i], !sel);
    }
}

void SolitaireActivity::render(RenderLock&&) {
    renderer.clearScreen();

    if (screen_ == Screen::MENU) {
        drawMenu();
        GUI.drawButtonHints(renderer, "Exit", "Start", "Up", "Down");
    } else {
        drawStatus();
        drawTopRow();
        drawTableau();
        drawBar();

        if (selSrc_ != Src::NONE) {
            if (selSrc_ == Src::WASTE) {
                drawFrame(colX(1) - 2, TOP_ROW_Y - 2, cardW_ + 4, cardH_ + 4, 4);
            } else if (selSrc_ == Src::FOUND) {
                drawFrame(colX(3 + selA_) - 2, TOP_ROW_Y - 2, cardW_ + 4, cardH_ + 4, 4);
            } else {
                const int y0 = cardY(selA_, selIdx_);
                const int y1 = cardY(selA_, game_->s.tabN[selA_] - 1) + cardH_;
                drawFrame(colX(selA_) - 2, y0 - 2, cardW_ + 4, y1 - y0 + 4, 4);
            }
        }

        const Target t = cursorTarget();
        if (!won_ && t.kind != Kind::NONE) {
            int x = 0;
            int y = 0;
            if (t.kind == Kind::TAB) {
                x = colX(t.a);
                y = t.idx >= 0 ? cardY(t.a, t.idx) : tabTop_;
            } else {
                x = colX(cursorCol_);
                y = TOP_ROW_Y;
            }
            constexpr int L = 12;
            constexpr int T = 3;
            renderer.fillRect(x + 1, y + 1, L, T, true);
            renderer.fillRect(x + 1, y + 1, T, L, true);
            renderer.fillRect(x + cardW_ - L - 1, y + 1, L, T, true);
            renderer.fillRect(x + cardW_ - T - 1, y + 1, T, L, true);
        }

        GUI.drawButtonHints(renderer, "Undo", "Select", "Left", "Right");
        GUI.drawSideButtonHints(renderer, "Up", "Down");
    }

    renderer.displayBuffer();
}
