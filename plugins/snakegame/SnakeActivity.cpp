#include "SnakeActivity.h"

#include <GfxRenderer.h>
#include "MappedInputManager.h"
#include "fontIds.h"

void SnakeActivity::onEnter() {
    Activity::onEnter();

    screenW_ = renderer.getScreenWidth();
    screenH_ = renderer.getScreenHeight();

    randomSeed(millis());
    resetGame();

    renderPending_ = true;
    requestUpdate();
}

void SnakeActivity::onExit() {
    Activity::onExit();
}

void SnakeActivity::resetGame() {
    cols_ = screenW_ / BLOCK;
    rows_ = (screenH_ - HEADER_H - FOOTER_H) / BLOCK;
    if (cols_ < 5) cols_ = 5;
    if (rows_ < 5) rows_ = 5;
    if ((long)cols_ * (long)rows_ > MAX_SNAKE_LEN) {
        rows_ = MAX_SNAKE_LEN / cols_;
    }

    fieldLeft_ = (screenW_ - cols_ * BLOCK) / 2;
    if (fieldLeft_ < 0) fieldLeft_ = 0;
    int freeVertical = screenH_ - HEADER_H - FOOTER_H - rows_ * BLOCK;
    fieldTop_ = HEADER_H + (freeVertical > 0 ? freeVertical / 2 : 0);

    int cx = cols_ / 2;
    int cy = rows_ / 2;

    headIdx_ = 0;
    length_  = 3;
    snakeX_[0] = cx;     snakeY_[0] = cy;
    snakeX_[1] = cx - 1; snakeY_[1] = cy;
    snakeX_[2] = cx - 2; snakeY_[2] = cy;

    dir_        = Dir::Right;
    pendingDir_ = Dir::Right;

    score_        = 0;
    tickInterval_ = TICK_START_MS;
    gameOver_     = false;
    won_          = false;
    lastMove_     = millis();

    placeFood();
}

bool SnakeActivity::isSnakeCell(int x, int y) const {
    for (int i = 0; i < length_; i++) {
        int idx = (headIdx_ + i) % MAX_SNAKE_LEN;
        if (snakeX_[idx] == x && snakeY_[idx] == y) return true;
    }
    return false;
}

void SnakeActivity::placeFood() {
    if (length_ >= cols_ * rows_) return;

    int fx, fy;
    do {
        fx = random(0, cols_);
        fy = random(0, rows_);
    } while (isSnakeCell(fx, fy));

    foodX_ = fx;
    foodY_ = fy;
}

void SnakeActivity::handleDirectionInput() {
    const int btn = mappedInput.getPressedFrontButton();

    switch (btn) {
        case HalGPIO::BTN_BACK:
            if (dir_ != Dir::Right) pendingDir_ = Dir::Left;
            break;
        case HalGPIO::BTN_CONFIRM:
            if (dir_ != Dir::Left) pendingDir_ = Dir::Right;
            break;
        case HalGPIO::BTN_LEFT:
            if (dir_ != Dir::Down) pendingDir_ = Dir::Up;
            break;
        case HalGPIO::BTN_RIGHT:
            if (dir_ != Dir::Up) pendingDir_ = Dir::Down;
            break;
        default:
            break;
    }
}

void SnakeActivity::updateGame() {
    dir_ = pendingDir_;

    int dx = 0, dy = 0;
    switch (dir_) {
        case Dir::Up:    dy = -1; break;
        case Dir::Down:  dy = 1;  break;
        case Dir::Left:  dx = -1; break;
        case Dir::Right: dx = 1;  break;
    }

    int newX = snakeX_[headIdx_] + dx;
    int newY = snakeY_[headIdx_] + dy;

    if (newX < 0 || newX >= cols_ || newY < 0 || newY >= rows_) {
        gameOver_ = true;
        return;
    }

    bool willEat = (newX == foodX_ && newY == foodY_);

    for (int i = 0; i < length_; i++) {
        if (!willEat && i == length_ - 1) continue;
        int idx = (headIdx_ + i) % MAX_SNAKE_LEN;
        if (snakeX_[idx] == newX && snakeY_[idx] == newY) {
            gameOver_ = true;
            return;
        }
    }

    headIdx_ = (headIdx_ - 1 + MAX_SNAKE_LEN) % MAX_SNAKE_LEN;
    snakeX_[headIdx_] = newX;
    snakeY_[headIdx_] = newY;

    if (willEat) {
        if (length_ < MAX_SNAKE_LEN) length_++;
        score_ += POINTS_PER_FOOD;

        if (length_ >= cols_ * rows_) {
            gameOver_ = true;
            won_      = true;
            return;
        }

        if (score_ % SPEEDUP_EVERY_POINTS == 0 && tickInterval_ > TICK_MIN_MS) {
            tickInterval_ -= TICK_STEP_MS;
            if (tickInterval_ < TICK_MIN_MS) tickInterval_ = TICK_MIN_MS;
        }

        placeFood();
    }
}

void SnakeActivity::loop() {
    if (mappedInput.wasPressed(MappedInputManager::Button::Power)) {
        while (mappedInput.isPressed(MappedInputManager::Button::Power)) {
            mappedInput.update();
            delay(10);
        }
        finish();
        return;
    }

    if (gameOver_) {
        if (mappedInput.getPressedFrontButton() != -1) {
            resetGame();
            renderPending_ = true;
            requestUpdate();
        }
        return;
    }

    handleDirectionInput();

    unsigned long now = millis();
    if (now - lastMove_ >= tickInterval_) {
        lastMove_ = now;
        updateGame();
        renderPending_ = true;
    }

    if (renderPending_) {
        requestUpdate();
    }
}

void SnakeActivity::drawFooterLabels() const {
    static const char* labels[4] = {"Left", "Right", "Up", "Down"};

    int footerTop = screenH_ - FOOTER_H;
    renderer.drawLine(0, footerTop, screenW_, footerTop);

    int colW = screenW_ / 4;
    int textH = renderer.getTextHeight(SMALL_FONT_ID);
    int labelY = footerTop + (FOOTER_H - textH) / 2;

    for (int i = 0; i < 4; i++) {
        int w = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
        int colCenterX = i * colW + colW / 2;
        renderer.drawText(SMALL_FONT_ID, colCenterX - w / 2, labelY, labels[i], true);

        if (i > 0) {
            renderer.drawLine(i * colW, footerTop, i * colW, screenH_);
        }
    }
}

void SnakeActivity::render(RenderLock&&) {
    renderPending_ = false;

    renderer.clearScreen();

    char scoreStr[24];
    snprintf(scoreStr, sizeof(scoreStr), "Score: %d", score_);
    renderer.drawText(UI_10_FONT_ID, 12, 10, scoreStr, true);

    int fieldW = cols_ * BLOCK;
    int fieldH = rows_ * BLOCK;

    renderer.drawLine(fieldLeft_, fieldTop_, fieldLeft_ + fieldW, fieldTop_);
    renderer.drawLine(fieldLeft_, fieldTop_ + fieldH, fieldLeft_ + fieldW, fieldTop_ + fieldH);
    renderer.drawLine(fieldLeft_, fieldTop_, fieldLeft_, fieldTop_ + fieldH);
    renderer.drawLine(fieldLeft_ + fieldW, fieldTop_, fieldLeft_ + fieldW, fieldTop_ + fieldH);

    for (int i = 0; i < length_; i++) {
        int idx = (headIdx_ + i) % MAX_SNAKE_LEN;
        int x = fieldLeft_ + snakeX_[idx] * BLOCK;
        int y = fieldTop_ + snakeY_[idx] * BLOCK;
        renderer.fillRect(x + 1, y + 1, BLOCK - 2, BLOCK - 2, true);

        if (i == 0) {
            int innerSize = BLOCK / 3;
            renderer.fillRect(x + (BLOCK - innerSize) / 2, y + (BLOCK - innerSize) / 2,
                               innerSize, innerSize, false);
        }
    }

    int fx = fieldLeft_ + foodX_ * BLOCK;
    int fy = fieldTop_ + foodY_ * BLOCK;
    renderer.fillRect(fx + 1, fy + 1, BLOCK - 2, BLOCK - 2, true);
    renderer.fillRect(fx + 4, fy + 4, BLOCK - 8, BLOCK - 8, false);

    drawFooterLabels();

    if (gameOver_) {
        char line2[24];
        snprintf(line2, sizeof(line2), "Score: %d", score_);

        int baseY = fieldTop_ + fieldH / 2 - 40;
        renderer.drawCenteredText(UI_12_FONT_ID, baseY, won_ ? "YOU WIN!" : "GAME OVER",
                                   true, EpdFontFamily::BOLD);
        renderer.drawCenteredText(UI_10_FONT_ID, baseY + 30, line2);
        renderer.drawCenteredText(UI_10_FONT_ID, baseY + 56, "Any button to Restart");
        renderer.drawCenteredText(UI_10_FONT_ID, baseY + 78, "Power button to Exit");
    }

    renderer.displayBuffer();
}
