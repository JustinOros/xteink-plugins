#pragma once

#include <memory>

#include "activities/Activity.h"
#include "CheckersEngine.h"

class CheckersActivity final : public Activity {
public:
    explicit CheckersActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
        : Activity("Checkers", renderer, mappedInput) {}

    void onEnter() override;
    void onExit() override;
    void loop() override;
    bool skipLoopDelay() override { return false; }
    void render(RenderLock&&) override;

private:
    enum class Screen { MODE, LEVEL, GAME };
    enum class Result { NONE, WHITE_WINS, BLACK_WINS, DRAW_FIFTY, DRAW_REPEAT };

    static constexpr int LEVEL_COUNT = 4;
    static constexpr int MODE_COUNT = 2;
    static constexpr int MAX_HIST = 400;
    static constexpr int MENU_TOP = 190;
    static constexpr int MENU_ROW_H = 84;

    static const checkers::Level LEVELS[LEVEL_COUNT];

    std::unique_ptr<checkers::Engine> engine_;
    std::unique_ptr<checkers::Position[]> hist_;
    int histCount_ = 0;

    checkers::Position pos_;
    Screen screen_ = Screen::MODE;
    Result result_ = Result::NONE;
    bool vsComputer_ = true;
    int menuIndex_ = 0;
    int level_ = 1;

    int selected_ = -1;
    int cursor_ = 41;
    int lastFrom_ = -1;
    int lastTo_ = -1;
    bool legalDest_[64];
    bool thinking_ = false;
    unsigned long resultAt_ = 0;

    int screenW_ = 0;
    int screenH_ = 0;
    int cellS_ = 0;
    int boardLeft_ = 0;
    int boardTop_ = 0;

    void startGame();
    void computeLayout();
    int squareAt(int x, int y) const;
    bool humanToMove() const;
    void onSquare(int sq);
    void selectSquare(int sq);
    void applyMove(const checkers::Move& m);
    void playHumanMove(int from, int to);
    void runEngine();
    void pushHistory();
    void undo();
    void checkEnd();
    int repetitionCount() const;
    void handleMenu();
    void handleGame();
    void drawMenu() const;
    void drawBoard() const;
    void drawPiece(int piece, int cx, int cy) const;
    void drawStatus() const;
    void drawBrackets(int x, int y) const;
    void fillCircle(int cx, int cy, int r, bool black) const;
};
