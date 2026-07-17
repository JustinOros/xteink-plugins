#pragma once

#include "activities/Activity.h"

class SnakeActivity final : public Activity {
public:
    explicit SnakeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
        : Activity("Snake", renderer, mappedInput) {}

    void onEnter() override;
    void onExit() override;
    void loop() override;
    bool skipLoopDelay() override { return true; }
    void render(RenderLock&&) override;

private:
    enum class Dir { Up, Down, Left, Right };

    static constexpr int BLOCK               = 18;
    static constexpr int HEADER_H            = 34;
    static constexpr int FOOTER_H            = 30;
    static constexpr int MAX_SNAKE_LEN       = 2048;
    static constexpr unsigned long TICK_START_MS = 170;
    static constexpr unsigned long TICK_MIN_MS   = 80;
    static constexpr unsigned long TICK_STEP_MS  = 6;
    static constexpr int POINTS_PER_FOOD     = 10;
    static constexpr int SPEEDUP_EVERY_POINTS = 50;

    int screenW_ = 0;
    int screenH_ = 0;
    int cols_    = 0;
    int rows_    = 0;
    int fieldLeft_ = 0;
    int fieldTop_  = 0;

    int snakeX_[MAX_SNAKE_LEN];
    int snakeY_[MAX_SNAKE_LEN];
    int headIdx_ = 0;
    int length_  = 0;

    Dir dir_        = Dir::Right;
    Dir pendingDir_ = Dir::Right;

    int foodX_ = 0;
    int foodY_ = 0;

    int score_ = 0;
    unsigned long tickInterval_ = TICK_START_MS;

    bool gameOver_      = false;
    bool won_           = false;
    bool renderPending_ = false;

    unsigned long lastMove_ = 0;

    void resetGame();
    void placeFood();
    bool isSnakeCell(int x, int y) const;
    void handleDirectionInput();
    void updateGame();
    void drawFooterLabels() const;
};
