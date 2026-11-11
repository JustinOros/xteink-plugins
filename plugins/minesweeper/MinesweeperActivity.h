#pragma once

#include <memory>

#include "activities/Activity.h"
#include "MinesweeperBoard.h"

class MinesweeperActivity final : public Activity {
public:
    explicit MinesweeperActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
        : Activity("Minesweeper", renderer, mappedInput) {}

    void onEnter() override;
    void onExit() override;
    void loop() override;
    bool skipLoopDelay() override { return false; }
    void render(RenderLock&&) override;

private:
    enum class Screen { MENU, GAME };

    struct Difficulty {
        const char* name;
        const char* hint;
        int cols;
        int rows;
        int mines;
    };

    static constexpr int LEVEL_COUNT = 3;
    static constexpr int MENU_TOP = 190;
    static constexpr int MENU_ROW_H = 84;
    static constexpr int BAR_H = 56;
    static constexpr int BOARD_TOP = 100;

    static const Difficulty LEVELS[LEVEL_COUNT];

    minesweeper::Board board_;
    Screen screen_ = Screen::MENU;
    int menuIndex_ = 0;
    int level_ = 0;
    bool flagMode_ = false;
    int cursor_ = 0;
    unsigned long startMs_ = 0;
    unsigned long endMs_ = 0;
    unsigned long endedAt_ = 0;

    int screenW_ = 0;
    int screenH_ = 0;
    int cellS_ = 0;
    int boardLeft_ = 0;
    int boardTop_ = 0;
    int barY_ = 0;

    void startGame(int level);
    void computeLayout();
    int cellAt(int x, int y) const;
    void dig(int idx);
    void flag(int idx);
    void afterAction(minesweeper::Outcome o);
    void handleMenu();
    void handleGame();
    void drawMenu() const;
    void drawBoard() const;
    void drawCell(int idx) const;
    void drawFlag(int cx, int cy, int s) const;
    void drawMine(int cx, int cy, int s, bool inverted) const;
    void drawStatus() const;
    void drawModeBar() const;
    void drawBrackets(int x, int y) const;
    void fillCircle(int cx, int cy, int r, bool black) const;
    bool over() const { return board_.outcome() != minesweeper::Outcome::NONE; }
};
