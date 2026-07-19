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

    int minX = (cols_ > 2) ? 1 : 0;
    int maxX = (cols_ > 2) ? cols_ - 2 : cols_ - 1;
    int minY = (rows_ > 2) ? 1 : 0;
    int maxY = (rows_ > 2) ? rows_ - 2 : rows_ - 1;

    int fx, fy;
    do {
        fx = random(minX, maxX + 1);
        fy = random(minY, maxY + 1);
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
        gameOver_   = true;
        gameOverAt_ = millis();
        return;
    }

    bool willEat = (newX == foodX_ && newY == foodY_);

    for (int i = 0; i < length_; i++) {
        if (!willEat && i == length_ - 1) continue;
        int idx = (headIdx_ + i) % MAX_SNAKE_LEN;
        if (snakeX_[idx] == newX && snakeY_[idx] == newY) {
            gameOver_   = true;
            gameOverAt_ = millis();
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
            gameOver_   = true;
            won_        = true;
            gameOverAt_ = millis();
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
        if (millis() - gameOverAt_ >= GAME_OVER_LOCKOUT_MS &&
            mappedInput.getPressedFrontButton() != -1) {
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

void SnakeActivity::drawArrow(Dir dir, int cx, int cy, int size) const {
    int half = size / 2;
    int xs[3];
    int ys[3];

    switch (dir) {
        case Dir::Left:
            xs[0] = cx - half; ys[0] = cy;
            xs[1] = cx + half; ys[1] = cy - half;
            xs[2] = cx + half; ys[2] = cy + half;
            break;
        case Dir::Right:
            xs[0] = cx + half; ys[0] = cy;
            xs[1] = cx - half; ys[1] = cy - half;
            xs[2] = cx - half; ys[2] = cy + half;
            break;
        case Dir::Up:
            xs[0] = cx;        ys[0] = cy - half;
            xs[1] = cx - half; ys[1] = cy + half;
            xs[2] = cx + half; ys[2] = cy + half;
            break;
        case Dir::Down:
            xs[0] = cx;        ys[0] = cy + half;
            xs[1] = cx - half; ys[1] = cy - half;
            xs[2] = cx + half; ys[2] = cy - half;
            break;
    }

    renderer.fillPolygon(xs, ys, 3, true);
}

void SnakeActivity::drawFooterLabels() const {
    static const Dir dirs[4] = {Dir::Left, Dir::Right, Dir::Up, Dir::Down};

    int footerTop = screenH_ - FOOTER_H;
    renderer.drawLine(0, footerTop, screenW_, footerTop);

    int colW = screenW_ / 4;
    int cy = screenH_ - ARROW_BOTTOM_PAD - ARROW_SIZE / 2;

    for (int i = 0; i < 4; i++) {
        int colCenterX = i * colW + colW / 2;
        drawArrow(dirs[i], colCenterX, cy, ARROW_SIZE);

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
        int baseY = fieldTop_ + fieldH / 2 - 30;
        renderer.drawCenteredText(UI_12_FONT_ID, baseY, won_ ? "YOU WIN!" : "GAME OVER",
                                   true, EpdFontFamily::BOLD);
        renderer.drawCenteredText(UI_10_FONT_ID, baseY + 30, "Any button to Restart");
        renderer.drawCenteredText(UI_10_FONT_ID, baseY + 52, "Power button to Exit");
    }

    renderer.displayBuffer();
}
