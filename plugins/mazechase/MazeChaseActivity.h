#pragma once

#include "activities/Activity.h"

class MazeChaseActivity final : public Activity {
public:
    explicit MazeChaseActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
        : Activity("MazeChase", renderer, mappedInput) {}

    void onEnter() override;
    void onExit() override;
    void loop() override;
    bool skipLoopDelay() override { return true; }
    void render(RenderLock&&) override;

private:
    enum class Dir { Up, Down, Left, Right };

    static constexpr int CELL      = 24;
    static constexpr int HEADER_H  = 34;
    static constexpr int FOOTER_H  = 38;
    static constexpr int ARROW_SIZE       = 16;
    static constexpr int ARROW_BOTTOM_PAD = 14;
    static constexpr int MAX_COLS  = 33;
    static constexpr int MAX_ROWS  = 45;
    static constexpr int ENEMIES   = 3;
    static constexpr int START_LIVES = 3;
    static constexpr int FRIGHT_TICKS = 28;
    static constexpr unsigned long TICK_START_MS = 280;
    static constexpr unsigned long TICK_MIN_MS   = 170;
    static constexpr unsigned long TICK_STEP_MS  = 15;
    static constexpr unsigned long GAME_OVER_LOCKOUT_MS = 3000;

    struct Enemy {
        int x;
        int y;
        Dir dir;
        int wait;
    };

    int screenW_ = 0;
    int screenH_ = 0;
    int cols_ = 0;
    int rows_ = 0;
    int fieldLeft_ = 0;
    int fieldTop_  = 0;

    bool wall_[MAX_ROWS][MAX_COLS];
    unsigned char pellet_[MAX_ROWS][MAX_COLS];
    short stack_[MAX_ROWS * MAX_COLS];
    int pelletsLeft_ = 0;

    int playerX_ = 0;
    int playerY_ = 0;
    int startX_  = 0;
    int startY_  = 0;
    int homeX_   = 0;
    int homeY_   = 0;
    Dir dir_     = Dir::Left;
    Dir wantDir_ = Dir::Left;
    bool moving_ = false;

    Enemy enemies_[ENEMIES];
    int frightTicks_ = 0;
    int tickCount_   = 0;

    int score_ = 0;
    int lives_ = START_LIVES;
    int level_ = 1;
    unsigned long tickInterval_ = TICK_START_MS;

    bool gameOver_      = false;
    bool renderPending_ = false;
    unsigned long lastMove_   = 0;
    unsigned long gameOverAt_ = 0;

    void newGame();
    void buildLevel();
    void carveMaze();
    void openDeadEnds();
    void placePellets();
    void resetPositions();
    bool isWall(int x, int y) const;
    bool canMove(int x, int y, Dir d) const;
    static void step(Dir d, int& x, int& y);
    static Dir opposite(Dir d);
    void handleInput();
    void updateGame();
    void moveEnemy(int index);
    void checkHits();
    void loseLife();
    void drawArrow(Dir dir, int cx, int cy, int size) const;
    void drawFooterLabels() const;
    void drawWalls() const;
    void drawPellets() const;
    void drawPlayer() const;
    void drawEnemy(const Enemy& e) const;
};
