#pragma once

#include <memory>

#include "activities/Activity.h"
#include "SudokuPuzzle.h"

class SudokuActivity final : public Activity {
public:
    explicit SudokuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
        : Activity("Sudoku", renderer, mappedInput) {}

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
        int givens;
    };

    struct UndoEntry {
        uint8_t cell;
        uint8_t value;
        uint16_t notes;
    };

    static constexpr int LEVEL_COUNT = 3;
    static constexpr int MENU_TOP = 190;
    static constexpr int MENU_ROW_H = 84;
    static constexpr int MAX_UNDO = 300;
    static constexpr int BOARD_TOP = 84;

    static const Difficulty LEVELS[LEVEL_COUNT];

    std::unique_ptr<sudoku::Solver> solver_;
    uint8_t grid_[81];
    uint8_t given_[81];
    uint8_t solution_[81];
    uint16_t notes_[81];
    UndoEntry undo_[MAX_UNDO];
    int undoCount_ = 0;

    Screen screen_ = Screen::MENU;
    int menuIndex_ = 0;
    int level_ = 0;
    int cursor_ = 40;
    bool notesMode_ = false;
    bool generating_ = false;
    bool solved_ = false;
    int hints_ = 0;
    unsigned long startMs_ = 0;
    unsigned long endMs_ = 0;
    unsigned long solvedAt_ = 0;

    int screenW_ = 0;
    int screenH_ = 0;
    int cellS_ = 0;
    int boardLeft_ = 0;
    int padTop_ = 0;
    int padH_ = 0;
    int actTop_ = 0;
    int actH_ = 0;

    void computeLayout();
    void generate();
    void pushUndo(int cell);
    void setDigit(int d);
    void eraseCell();
    void undo();
    void hint();
    void checkSolved();
    bool conflict(int cell) const;
    int digitCount(int d) const;
    int cellAt(int x, int y) const;
    void handleMenu();
    void handleGame();
    void handleTap(int x, int y);
    void drawMenu() const;
    void drawBoard() const;
    void drawPad() const;
    void drawStatus() const;
    void drawButton(int x, int y, int w, int h, const char* label, bool selected, bool dim) const;
};
