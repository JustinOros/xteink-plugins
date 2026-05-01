#include "PongActivity.h"

#include <GfxRenderer.h>
#include "MappedInputManager.h"
#include "fontIds.h"

void PongActivity::onEnter() {
    Activity::onEnter();

    screenW_ = renderer.getScreenWidth();
    screenH_ = renderer.getScreenHeight();

    playerX_     = (screenW_ - PADDLE_W) / 2.0f;
    cpuX_        = (screenW_ - PADDLE_W) / 2.0f;
    playerScore_ = 0;
    cpuScore_    = 0;
    gameOver_    = false;
    playerWon_   = false;
    lastUpdate_  = millis();

    resetBall(true);
    renderPending_ = true;
    requestUpdate();
}

void PongActivity::onExit() {
    Activity::onExit();
}

void PongActivity::resetBall(bool playerServes) {
    ballX_ = screenW_ / 2.0f;
    ballY_ = screenH_ / 2.0f;
    ballDX_ = (playerServes ? 1.0f : -1.0f) * BALL_SPEED_X;
    ballDY_ = BALL_SPEED_Y;
}

void PongActivity::moveCpu() {
    float cpuCenterX = cpuX_ + PADDLE_W / 2.0f;
    float ballCenterX = ballX_ + BALL_SIZE / 2.0f;

    if (ballCenterX < cpuCenterX - 4) {
        cpuX_ -= CPU_SPEED;
    } else if (ballCenterX > cpuCenterX + 4) {
        cpuX_ += CPU_SPEED;
    }

    if (cpuX_ < MARGIN) cpuX_ = MARGIN;
    if (cpuX_ + PADDLE_W > screenW_ - MARGIN) cpuX_ = screenW_ - MARGIN - PADDLE_W;
}

void PongActivity::updateGame() {
    ballX_ += ballDX_;
    ballY_ += ballDY_;

    if (ballX_ <= MARGIN) {
        ballX_ = MARGIN;
        ballDX_ = -ballDX_;
    }
    if (ballX_ + BALL_SIZE >= screenW_ - MARGIN) {
        ballX_ = screenW_ - MARGIN - BALL_SIZE;
        ballDX_ = -ballDX_;
    }

    int playerPaddleY = screenH_ - MARGIN - PADDLE_H;
    if (ballDY_ > 0 &&
        ballY_ + BALL_SIZE >= playerPaddleY &&
        ballY_ + BALL_SIZE <= playerPaddleY + PADDLE_H + 4 &&
        ballX_ + BALL_SIZE >= playerX_ &&
        ballX_ <= playerX_ + PADDLE_W)
    {
        ballY_ = playerPaddleY - BALL_SIZE;
        ballDY_ = -ballDY_;
        float hitPos = (ballX_ + BALL_SIZE / 2.0f) - (playerX_ + PADDLE_W / 2.0f);
        ballDX_ = hitPos / (PADDLE_W / 2.0f) * BALL_SPEED_X * 1.5f;
        if (ballDX_ > BALL_SPEED_X * 2) ballDX_ = BALL_SPEED_X * 2;
        if (ballDX_ < -BALL_SPEED_X * 2) ballDX_ = -BALL_SPEED_X * 2;
    }

    int cpuPaddleY = MARGIN;
    if (ballDY_ < 0 &&
        ballY_ <= cpuPaddleY + PADDLE_H &&
        ballY_ >= cpuPaddleY - 4 &&
        ballX_ + BALL_SIZE >= cpuX_ &&
        ballX_ <= cpuX_ + PADDLE_W)
    {
        ballY_ = cpuPaddleY + PADDLE_H;
        ballDY_ = -ballDY_;
        float hitPos = (ballX_ + BALL_SIZE / 2.0f) - (cpuX_ + PADDLE_W / 2.0f);
        ballDX_ = hitPos / (PADDLE_W / 2.0f) * BALL_SPEED_X * 1.5f;
        if (ballDX_ > BALL_SPEED_X * 2) ballDX_ = BALL_SPEED_X * 2;
        if (ballDX_ < -BALL_SPEED_X * 2) ballDX_ = -BALL_SPEED_X * 2;
    }

    if (ballY_ > screenH_) {
        cpuScore_++;
        if (cpuScore_ >= SCORE_TO_WIN) {
            gameOver_ = true;
            playerWon_ = false;
        } else {
            resetBall(false);
        }
    }

    if (ballY_ + BALL_SIZE < 0) {
        playerScore_++;
        if (playerScore_ >= SCORE_TO_WIN) {
            gameOver_ = true;
            playerWon_ = true;
        } else {
            resetBall(true);
        }
    }

    moveCpu();
}

void PongActivity::loop() {
    if (gameOver_) {
        if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
            mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
            mappedInput.wasPressed(MappedInputManager::Button::Left) ||
            mappedInput.wasPressed(MappedInputManager::Button::Right) ||
            mappedInput.wasPressed(MappedInputManager::Button::Up) ||
            mappedInput.wasPressed(MappedInputManager::Button::Down) ||
            mappedInput.wasPressed(MappedInputManager::Button::Power)) {
            finish();
        }
        return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Power)) {
        while (mappedInput.isPressed(MappedInputManager::Button::Power)) {
            mappedInput.update();
            delay(10);
        }
        finish();
        return;
    }

    bool moved = false;

    bool goLeft  = mappedInput.wasPressed(MappedInputManager::Button::Left) ||
                   mappedInput.wasPressed(MappedInputManager::Button::Up) ||
                   mappedInput.wasPressed(MappedInputManager::Button::Back);
    bool goRight = mappedInput.wasPressed(MappedInputManager::Button::Right) ||
                   mappedInput.wasPressed(MappedInputManager::Button::Down) ||
                   mappedInput.wasPressed(MappedInputManager::Button::Confirm);

    if (goLeft) {
        playerX_ -= PADDLE_W;
        if (playerX_ < MARGIN) playerX_ = MARGIN;
        moved = true;
    }
    if (goRight) {
        playerX_ += PADDLE_W;
        if (playerX_ + PADDLE_W > screenW_ - MARGIN) playerX_ = screenW_ - MARGIN - PADDLE_W;
        moved = true;
    }


    unsigned long now = millis();
    if (now - lastUpdate_ >= 33) {
        lastUpdate_ = now;
        updateGame();
        renderPending_ = true;
        requestUpdate();
    } else if (moved) {
        renderPending_ = true;
        requestUpdate();
    }
}

void PongActivity::render(RenderLock&&) {
    renderPending_ = false;

    renderer.clearScreen();

    renderer.drawLine(MARGIN, MARGIN, screenW_ - MARGIN, MARGIN);
    renderer.drawLine(MARGIN, screenH_ - MARGIN, screenW_ - MARGIN, screenH_ - MARGIN);
    renderer.drawLine(MARGIN, MARGIN, MARGIN, screenH_ - MARGIN);
    renderer.drawLine(screenW_ - MARGIN, MARGIN, screenW_ - MARGIN, screenH_ - MARGIN);

    int midY = screenH_ / 2;
    for (int x = MARGIN + 4; x < screenW_ - MARGIN; x += 16) {
        renderer.drawLine(x, midY, x + 8, midY);
    }

    int cpuPaddleY = MARGIN;
    renderer.fillRect((int)cpuX_, cpuPaddleY, PADDLE_W, PADDLE_H, true);

    int playerPaddleY = screenH_ - MARGIN - PADDLE_H;
    renderer.fillRect((int)playerX_, playerPaddleY, PADDLE_W, PADDLE_H, true);

    renderer.fillRect((int)ballX_, (int)ballY_, BALL_SIZE, BALL_SIZE, true);

    char scoreStr[16];
    snprintf(scoreStr, sizeof(scoreStr), "%d  %d", cpuScore_, playerScore_);
    int scoreX = screenW_ / 2 - 20;
    renderer.drawText(UI_10_FONT_ID, scoreX, midY - 20, scoreStr, true);

    if (gameOver_) {
        int quarterY = screenH_ / 4;
        const char* msg = playerWon_ ? "YOU WIN!" : "CPU WINS";
        renderer.drawText(UI_12_FONT_ID, screenW_ / 2 - 50, quarterY - 20, msg, true, EpdFontFamily::BOLD);
        renderer.drawText(UI_10_FONT_ID, screenW_ / 2 - 80, quarterY + 20, "Press any button to exit", true);
    }

    renderer.displayBuffer();
}
