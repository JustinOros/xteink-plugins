#include "MazeChaseActivity.h"

#include <GfxRenderer.h>
#include "MappedInputManager.h"
#include "fontIds.h"

void MazeChaseActivity::onEnter() {
    Activity::onEnter();

    screenW_ = renderer.getScreenWidth();
    screenH_ = renderer.getScreenHeight();

    randomSeed(millis());
    newGame();

    renderPending_ = true;
    requestUpdate();
}

void MazeChaseActivity::onExit() {
    Activity::onExit();
}

void MazeChaseActivity::newGame() {
    score_ = 0;
    lives_ = START_LIVES;
    level_ = 1;
    gameOver_ = false;
    buildLevel();
}

void MazeChaseActivity::buildLevel() {
    cols_ = screenW_ / CELL;
    if (cols_ > MAX_COLS) cols_ = MAX_COLS;
    if (cols_ % 2 == 0) cols_--;
    rows_ = (screenH_ - HEADER_H - FOOTER_H) / CELL;
    if (rows_ > MAX_ROWS) rows_ = MAX_ROWS;
    if (rows_ % 2 == 0) rows_--;

    fieldLeft_ = (screenW_ - cols_ * CELL) / 2;
    int freeVertical = screenH_ - HEADER_H - FOOTER_H - rows_ * CELL;
    fieldTop_ = HEADER_H + (freeVertical > 0 ? freeVertical / 2 : 0);

    carveMaze();
    openDeadEnds();
    placePellets();

    startX_ = (cols_ / 2) | 1;
    startY_ = rows_ - 2;
    homeX_  = (cols_ / 2) | 1;
    homeY_  = (rows_ / 2) | 1;

    int speed = (int)TICK_START_MS - (level_ - 1) * (int)TICK_STEP_MS;
    if (speed < (int)TICK_MIN_MS) speed = (int)TICK_MIN_MS;
    tickInterval_ = (unsigned long)speed;

    resetPositions();
    lastMove_ = millis();
}

void MazeChaseActivity::carveMaze() {
    for (int y = 0; y < rows_; y++) {
        for (int x = 0; x < cols_; x++) {
            wall_[y][x] = true;
        }
    }

    int top = 0;
    wall_[1][1] = false;
    stack_[top++] = (short)(1 * MAX_COLS + 1);

    while (top > 0) {
        int cur = stack_[top - 1];
        int cx = cur % MAX_COLS;
        int cy = cur / MAX_COLS;

        int options[4];
        int count = 0;
        const int dx[4] = {0, 0, -2, 2};
        const int dy[4] = {-2, 2, 0, 0};
        for (int i = 0; i < 4; i++) {
            int nx = cx + dx[i];
            int ny = cy + dy[i];
            if (nx > 0 && nx < cols_ - 1 && ny > 0 && ny < rows_ - 1 && wall_[ny][nx]) {
                options[count++] = i;
            }
        }

        if (count == 0) {
            top--;
            continue;
        }

        int pick = options[random(0, count)];
        int nx = cx + dx[pick];
        int ny = cy + dy[pick];
        wall_[cy + dy[pick] / 2][cx + dx[pick] / 2] = false;
        wall_[ny][nx] = false;
        stack_[top++] = (short)(ny * MAX_COLS + nx);
    }
}

void MazeChaseActivity::openDeadEnds() {
    for (int y = 1; y < rows_ - 1; y++) {
        for (int x = 1; x < cols_ - 1; x++) {
            if (wall_[y][x]) continue;

            int open = 0;
            if (!wall_[y - 1][x]) open++;
            if (!wall_[y + 1][x]) open++;
            if (!wall_[y][x - 1]) open++;
            if (!wall_[y][x + 1]) open++;
            if (open != 1) continue;

            for (int tries = 0; tries < 8; tries++) {
                int d = random(0, 4);
                int wx = x;
                int wy = y;
                int bx = x;
                int by = y;
                switch (d) {
                    case 0: wy = y - 1; by = y - 2; break;
                    case 1: wy = y + 1; by = y + 2; break;
                    case 2: wx = x - 1; bx = x - 2; break;
                    default: wx = x + 1; bx = x + 2; break;
                }
                if (wx < 1 || wx > cols_ - 2 || wy < 1 || wy > rows_ - 2) continue;
                if (bx < 1 || bx > cols_ - 2 || by < 1 || by > rows_ - 2) continue;
                if (!wall_[wy][wx] || wall_[by][bx]) continue;
                wall_[wy][wx] = false;
                break;
            }
        }
    }
}

void MazeChaseActivity::placePellets() {
    pelletsLeft_ = 0;
    for (int y = 0; y < rows_; y++) {
        for (int x = 0; x < cols_; x++) {
            pellet_[y][x] = wall_[y][x] ? 0 : 1;
            if (!wall_[y][x]) pelletsLeft_++;
        }
    }

    pellet_[1][1] = 2;
    pellet_[1][cols_ - 2] = 2;
    pellet_[rows_ - 2][1] = 2;
    pellet_[rows_ - 2][cols_ - 2] = 2;
}

void MazeChaseActivity::resetPositions() {
    playerX_ = startX_;
    playerY_ = startY_;
    if (pellet_[playerY_][playerX_]) {
        if (pellet_[playerY_][playerX_] == 1) pelletsLeft_--;
        pellet_[playerY_][playerX_] = 0;
    }
    dir_ = Dir::Left;
    wantDir_ = Dir::Left;
    moving_ = false;
    frightTicks_ = 0;
    tickCount_ = 0;

    for (int i = 0; i < ENEMIES; i++) {
        enemies_[i].x = homeX_;
        enemies_[i].y = homeY_;
        enemies_[i].dir = Dir::Up;
        enemies_[i].wait = 4 + i * 8;
    }
}

bool MazeChaseActivity::isWall(int x, int y) const {
    if (x < 0 || x >= cols_ || y < 0 || y >= rows_) return true;
    return wall_[y][x];
}

void MazeChaseActivity::step(Dir d, int& x, int& y) {
    switch (d) {
        case Dir::Up:    y--; break;
        case Dir::Down:  y++; break;
        case Dir::Left:  x--; break;
        case Dir::Right: x++; break;
    }
}

MazeChaseActivity::Dir MazeChaseActivity::opposite(Dir d) {
    switch (d) {
        case Dir::Up:    return Dir::Down;
        case Dir::Down:  return Dir::Up;
        case Dir::Left:  return Dir::Right;
        default:         return Dir::Left;
    }
}

bool MazeChaseActivity::canMove(int x, int y, Dir d) const {
    step(d, x, y);
    return !isWall(x, y);
}

void MazeChaseActivity::handleInput() {
    int tapX = 0;
    int tapY = 0;
    if (mappedInput.wasScreenTapped(tapX, tapY)) {
        const int px = fieldLeft_ + playerX_ * CELL + CELL / 2;
        const int py = fieldTop_ + playerY_ * CELL + CELL / 2;
        const int dx = tapX - px;
        const int dy = tapY - py;
        const bool horizontalFirst = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
        const Dir horizontal = dx >= 0 ? Dir::Right : Dir::Left;
        const Dir vertical = dy >= 0 ? Dir::Down : Dir::Up;
        wantDir_ = horizontalFirst ? horizontal : vertical;
    }

    switch (mappedInput.wasSwipe()) {
        case MappedInputManager::SwipeDir::Left:  wantDir_ = Dir::Left;  break;
        case MappedInputManager::SwipeDir::Right: wantDir_ = Dir::Right; break;
        case MappedInputManager::SwipeDir::Up:    wantDir_ = Dir::Up;    break;
        case MappedInputManager::SwipeDir::Down:  wantDir_ = Dir::Down;  break;
        default: break;
    }

    switch (mappedInput.getPressedFrontButton()) {
        case HalGPIO::BTN_BACK:    wantDir_ = Dir::Left;  break;
        case HalGPIO::BTN_CONFIRM: wantDir_ = Dir::Right; break;
        case HalGPIO::BTN_LEFT:    wantDir_ = Dir::Up;    break;
        case HalGPIO::BTN_RIGHT:   wantDir_ = Dir::Down;  break;
        default: break;
    }
}

void MazeChaseActivity::loseLife() {
    lives_--;
    if (lives_ <= 0) {
        gameOver_ = true;
        gameOverAt_ = millis();
        return;
    }
    resetPositions();
}

void MazeChaseActivity::checkHits() {
    for (int i = 0; i < ENEMIES; i++) {
        if (enemies_[i].x != playerX_ || enemies_[i].y != playerY_) continue;
        if (frightTicks_ > 0) {
            score_ += 200;
            enemies_[i].x = homeX_;
            enemies_[i].y = homeY_;
            enemies_[i].dir = Dir::Up;
            enemies_[i].wait = 6;
        } else {
            loseLife();
            return;
        }
    }
}

void MazeChaseActivity::moveEnemy(int index) {
    Enemy& e = enemies_[index];

    if (e.wait > 0) {
        e.wait--;
        return;
    }
    if (frightTicks_ > 0 && (tickCount_ & 1)) return;

    Dir options[4];
    int count = 0;
    const Dir all[4] = {Dir::Up, Dir::Down, Dir::Left, Dir::Right};
    for (int i = 0; i < 4; i++) {
        if (all[i] == opposite(e.dir)) continue;
        if (canMove(e.x, e.y, all[i])) options[count++] = all[i];
    }
    if (count == 0) {
        Dir back = opposite(e.dir);
        if (canMove(e.x, e.y, back)) options[count++] = back;
    }
    if (count == 0) return;

    int targetX = playerX_;
    int targetY = playerY_;
    if (index == 2) {
        for (int i = 0; i < 3; i++) {
            int nx = targetX;
            int ny = targetY;
            step(dir_, nx, ny);
            if (isWall(nx, ny)) break;
            targetX = nx;
            targetY = ny;
        }
    }

    bool randomMove = frightTicks_ == 0 && index == 1 && random(0, 100) < 40;

    Dir chosen = options[0];
    if (randomMove) {
        chosen = options[random(0, count)];
    } else {
        int bestScore = frightTicks_ > 0 ? -1 : 1 << 30;
        for (int i = 0; i < count; i++) {
            int nx = e.x;
            int ny = e.y;
            step(options[i], nx, ny);
            int dx = nx - targetX;
            int dy = ny - targetY;
            int dist = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            bool better = frightTicks_ > 0 ? dist > bestScore : dist < bestScore;
            if (better) {
                bestScore = dist;
                chosen = options[i];
            }
        }
    }

    e.dir = chosen;
    step(chosen, e.x, e.y);
}

void MazeChaseActivity::updateGame() {
    tickCount_++;

    if (canMove(playerX_, playerY_, wantDir_)) {
        dir_ = wantDir_;
        moving_ = true;
    } else if (!canMove(playerX_, playerY_, dir_)) {
        moving_ = false;
    }

    if (moving_) {
        step(dir_, playerX_, playerY_);
        unsigned char& cell = pellet_[playerY_][playerX_];
        if (cell == 1) {
            score_ += 10;
            pelletsLeft_--;
        } else if (cell == 2) {
            score_ += 50;
            pelletsLeft_--;
            frightTicks_ = FRIGHT_TICKS;
        }
        cell = 0;
    }

    checkHits();
    if (gameOver_) return;

    if (pelletsLeft_ <= 0) {
        level_++;
        buildLevel();
        return;
    }

    for (int i = 0; i < ENEMIES; i++) {
        moveEnemy(i);
    }

    checkHits();
    if (gameOver_) return;

    if (frightTicks_ > 0) frightTicks_--;
}

void MazeChaseActivity::loop() {
    if (mappedInput.wasPressed(MappedInputManager::Button::Power) || mappedInput.wasBackGesture()) {
        while (mappedInput.isPressed(MappedInputManager::Button::Power)) {
            mappedInput.update();
            delay(10);
        }
        finish();
        return;
    }

    if (gameOver_) {
        int tx = 0;
        int ty = 0;
        if (millis() - gameOverAt_ >= GAME_OVER_LOCKOUT_MS &&
            (mappedInput.getPressedFrontButton() != -1 || mappedInput.wasScreenTapped(tx, ty))) {
            newGame();
            renderPending_ = true;
            requestUpdate();
        }
        return;
    }

    handleInput();

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

void MazeChaseActivity::drawArrow(Dir dir, int cx, int cy, int size) const {
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

void MazeChaseActivity::drawFooterLabels() const {
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

void MazeChaseActivity::drawWalls() const {
    constexpr int T = 6;
    for (int y = 0; y < rows_; y++) {
        for (int x = 0; x < cols_; x++) {
            if (!wall_[y][x]) continue;
            const int cx = fieldLeft_ + x * CELL + CELL / 2;
            const int cy = fieldTop_ + y * CELL + CELL / 2;
            renderer.fillRect(cx - T / 2, cy - T / 2, T, T, true);
            if (x + 1 < cols_ && wall_[y][x + 1]) {
                renderer.fillRect(cx, cy - T / 2, CELL, T, true);
            }
            if (y + 1 < rows_ && wall_[y + 1][x]) {
                renderer.fillRect(cx - T / 2, cy, T, CELL, true);
            }
        }
    }
}

void MazeChaseActivity::drawPellets() const {
    for (int y = 0; y < rows_; y++) {
        for (int x = 0; x < cols_; x++) {
            const unsigned char kind = pellet_[y][x];
            if (kind == 0) continue;
            const int cx = fieldLeft_ + x * CELL + CELL / 2;
            const int cy = fieldTop_ + y * CELL + CELL / 2;
            if (kind == 1) {
                renderer.fillRect(cx - 2, cy - 2, 4, 4, true);
            } else {
                renderer.fillRect(cx - 7, cy - 7, 14, 14, true);
                renderer.fillRect(cx - 3, cy - 3, 6, 6, false);
            }
        }
    }
}

void MazeChaseActivity::drawPlayer() const {
    const int cx = fieldLeft_ + playerX_ * CELL + CELL / 2;
    const int cy = fieldTop_ + playerY_ * CELL + CELL / 2;
    const int size = CELL - 6;
    renderer.fillRoundedRect(cx - size / 2, cy - size / 2, size, size, 6, Color::Black);

    int ex = cx;
    int ey = cy;
    switch (dir_) {
        case Dir::Up:    ey -= 3; break;
        case Dir::Down:  ey += 3; break;
        case Dir::Left:  ex -= 3; break;
        case Dir::Right: ex += 3; break;
    }
    renderer.fillRect(ex - 2, ey - 2, 5, 5, false);
}

void MazeChaseActivity::drawEnemy(const Enemy& e) const {
    const int cx = fieldLeft_ + e.x * CELL + CELL / 2;
    const int cy = fieldTop_ + e.y * CELL + CELL / 2;
    const int r = CELL / 2 - 2;

    if (frightTicks_ > 0) {
        renderer.drawLine(cx, cy - r, cx + r, cy);
        renderer.drawLine(cx + r, cy, cx, cy + r);
        renderer.drawLine(cx, cy + r, cx - r, cy);
        renderer.drawLine(cx - r, cy, cx, cy - r);
        renderer.fillRect(cx - 1, cy - 1, 3, 3, true);
        return;
    }

    int xs[4] = {cx, cx + r, cx, cx - r};
    int ys[4] = {cy - r, cy, cy + r, cy};
    renderer.fillPolygon(xs, ys, 4, true);
    renderer.fillRect(cx - 2, cy - 2, 5, 5, false);
}

void MazeChaseActivity::render(RenderLock&&) {
    renderPending_ = false;

    renderer.clearScreen();

    char scoreStr[24];
    snprintf(scoreStr, sizeof(scoreStr), "Score: %d", score_);
    renderer.drawText(UI_10_FONT_ID, 12, 10, scoreStr, true);

    char infoStr[32];
    snprintf(infoStr, sizeof(infoStr), "Lives %d  Level %d", lives_, level_);
    const int infoW = renderer.getTextWidth(UI_10_FONT_ID, infoStr);
    renderer.drawText(UI_10_FONT_ID, screenW_ - infoW - 12, 10, infoStr, true);

    drawWalls();
    drawPellets();
    for (int i = 0; i < ENEMIES; i++) {
        drawEnemy(enemies_[i]);
    }
    drawPlayer();

    drawFooterLabels();

    if (gameOver_) {
        const int baseY = fieldTop_ + (rows_ * CELL) / 2 - 30;
        renderer.fillRect(fieldLeft_ + 20, baseY - 12, cols_ * CELL - 40, 100, false);
        renderer.drawCenteredText(UI_12_FONT_ID, baseY, "GAME OVER", true, EpdFontFamily::BOLD);
        renderer.drawCenteredText(UI_10_FONT_ID, baseY + 30, "Any button to Restart");
        renderer.drawCenteredText(UI_10_FONT_ID, baseY + 52, "Power button to Exit");
    }

    renderer.displayBuffer();
}
