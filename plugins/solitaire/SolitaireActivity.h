#pragma once

#include <memory>

#include "activities/Activity.h"
#include "SolitaireGame.h"

class SolitaireActivity final : public Activity {
public:
    explicit SolitaireActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
        : Activity("Solitaire", renderer, mappedInput) {}

    void onEnter() override;
    void onExit() override;
    void loop() override;
    bool skipLoopDelay() override { return false; }
    void render(RenderLock&&) override;

private:
    enum class Screen { MENU, GAME };
    enum class Kind { NONE, STOCK, WASTE, FOUND, TAB };

    struct Target {
        Kind kind;
        int a;
        int idx;
    };

    static constexpr int MENU_COUNT = 2;
    static constexpr int MENU_TOP = 190;
    static constexpr int MENU_ROW_H = 84;
    static constexpr int GAP = 4;
    static constexpr int BAR_H = 50;
    static constexpr int TOP_ROW_Y = 72;

    std::unique_ptr<solitaire::Game> game_;

    Screen screen_ = Screen::MENU;
    int menuIndex_ = 0;
    int drawCount_ = 1;

    solitaire::Src selSrc_ = solitaire::Src::NONE;
    int selA_ = 0;
    int selIdx_ = 0;

    int cursorRow_ = 1;
    int cursorCol_ = 0;
    int cursorIdx_ = 0;

    bool won_ = false;
    unsigned long startMs_ = 0;
    unsigned long endMs_ = 0;
    unsigned long wonAt_ = 0;
    unsigned long newArmedAt_ = 0;

    int screenW_ = 0;
    int screenH_ = 0;
    int cardW_ = 0;
    int cardH_ = 0;
    int tabTop_ = 0;
    int barY_ = 0;

    void computeLayout();
    void startGame(int draw);
    int colX(int col) const { return 4 + col * (cardW_ + GAP); }
    int upStep(int col) const;
    int cardY(int col, int idx) const;
    Target hitTest(int x, int y) const;
    Target cursorTarget() const;
    void clampCursor();
    void activate(const Target& t);
    void clearSelection() { selSrc_ = solitaire::Src::NONE; }
    void selectTarget(const Target& t);
    bool selectionIs(const Target& t) const;
    void afterMove();
    void handleMenu();
    void handleGame();
    void handleBar(int x);
    void drawMenu() const;
    void drawTopRow() const;
    void drawTableau() const;
    void drawBar() const;
    void drawStatus() const;
    void drawCard(int x, int y, int id, bool fullHeight) const;
    void drawBack(int x, int y) const;
    void drawEmpty(int x, int y) const;
    void drawSuit(int suit, int cx, int cy, int s) const;
    void drawFrame(int x, int y, int w, int h, int thick) const;
    void fillCircle(int cx, int cy, int r, bool black) const;
};
