#pragma once

#include "activities/Activity.h"

class PongActivity final : public Activity {
public:
    explicit PongActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
        : Activity("Pong", renderer, mappedInput) {}

    void onEnter() override;
    void onExit() override;
    void loop() override;
    bool skipLoopDelay() override { return true; }
    void render(RenderLock&&) override;

private:
    static constexpr int PADDLE_W       = 80;
    static constexpr int PADDLE_H       = 12;
    static constexpr int PADDLE_SPEED   = 8;
    static constexpr int BALL_SIZE      = 12;
    static constexpr int BALL_SPEED_X   = 4;
    static constexpr int BALL_SPEED_Y   = 4;
    static constexpr int MARGIN         = 20;
    static constexpr int CPU_SPEED      = 3;
    static constexpr int SCORE_TO_WIN   = 7;

    int screenW_ = 0;
    int screenH_ = 0;

    float playerX_  = 0;
    float cpuX_     = 0;

    float ballX_    = 0;
    float ballY_    = 0;
    float ballDX_   = 0;
    float ballDY_   = 0;

    int playerScore_ = 0;
    int cpuScore_    = 0;

    bool gameOver_      = false;
    bool playerWon_     = false;
    bool renderPending_ = false;

    unsigned long lastUpdate_ = 0;

    void resetBall(bool playerServes);
    void updateGame();
    void moveCpu();
};
