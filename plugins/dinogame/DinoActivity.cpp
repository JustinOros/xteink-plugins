#include "DinoActivity.h"

#include <GfxRenderer.h>
#include "MappedInputManager.h"
#include "fontIds.h"

namespace {

struct SpriteRow {
    uint8_t row, c0, c1;
};

constexpr int DINO_GRID_W = 22;
constexpr int DINO_GRID_H = 20;
constexpr int DINO_COLLIDE_X_OFFSET = 6;
constexpr int CACTUS_SMALL_W = 15;
constexpr int CACTUS_SMALL_H = 20;
constexpr int CACTUS_BIG_W   = 15;
constexpr int CACTUS_BIG_H   = 26;
constexpr int BIRD_W = 18;
constexpr int BIRD_H = 7;

constexpr SpriteRow DINO_HEAD_BODY[] = {
    {0,14,19},{1,13,20},{2,13,20},{3,13,20},{4,13,21},{5,13,21},{6,10,19},
    {7,11,17},{8,9,18},{9,6,18},{10,0,15},{11,0,15},{12,0,15},
    {13,0,0},{13,4,15},{14,8,15},
};

constexpr SpriteRow DINO_RUN1_LEGS[] = {
    {15,6,9},{16,6,9},{17,6,9},{18,6,9},{19,5,9},
    {15,11,14},{16,11,14},{17,11,14},
};

constexpr SpriteRow DINO_RUN2_LEGS[] = {
    {15,6,9},{16,6,9},{17,6,9},
    {15,11,14},{16,11,14},{17,11,14},{18,11,14},{19,11,15},
};

constexpr SpriteRow DINO_JUMP_LEGS[] = {
    {15,7,14},{16,7,14},{17,7,14},
};

constexpr SpriteRow CACTUS_SMALL[] = {
    {0,7,7},
    {1,6,8},
    {2,3,3},{2,6,8},{2,11,11},
    {3,2,4},{3,6,8},{3,10,12},
    {4,2,4},{4,6,8},{4,10,12},
    {5,2,4},{5,6,8},{5,10,12},
    {6,2,4},{6,6,8},{6,10,12},
    {7,2,4},{7,6,8},{7,10,12},
    {8,2,4},{8,6,8},{8,10,12},
    {9,2,12},
    {10,2,12},
    {11,6,8},{12,6,8},{13,6,8},{14,6,8},{15,6,8},
    {16,6,8},{17,6,8},{18,6,8},{19,6,8},
};

constexpr SpriteRow CACTUS_BIG[] = {
    {0,7,7},
    {1,6,8},
    {2,3,3},{2,6,8},{2,11,11},
    {3,2,4},{3,6,8},{3,10,12},
    {4,2,4},{4,6,8},{4,10,12},
    {5,2,4},{5,6,8},{5,10,12},
    {6,2,4},{6,6,8},{6,10,12},
    {7,2,4},{7,6,8},{7,10,12},
    {8,2,4},{8,6,8},{8,10,12},
    {9,2,12},
    {10,2,12},
    {11,6,8},{12,6,8},{13,6,8},{14,6,8},{15,6,8},
    {16,6,8},{17,6,8},{18,6,8},{19,6,8},{20,6,8},{21,6,8},
    {22,6,8},{23,6,8},{24,6,8},{25,6,8},
};

constexpr SpriteRow BIRD_UP[] = {
    {0,2,3},{0,14,15},
    {1,4,5},{1,12,13},
    {2,6,7},{2,10,11},
    {3,6,11},
    {4,7,10},
    {5,8,9},
};

constexpr SpriteRow BIRD_DOWN[] = {
    {0,7,10},
    {1,6,11},
    {2,7,10},
    {3,8,9},
    {4,6,7},{4,10,11},
    {5,4,5},{5,12,13},
    {6,2,3},{6,14,15},
};

template <size_t N>
constexpr size_t lengthOf(const SpriteRow (&)[N]) { return N; }

void drawSprite(GfxRenderer& r, const SpriteRow* rows, size_t count, int x, int y, int block) {
    for (size_t i = 0; i < count; i++) {
        int w = (rows[i].c1 - rows[i].c0 + 1) * block;
        r.fillRect(x + rows[i].c0 * block, y + rows[i].row * block, w, block, true);
    }
}

}  // namespace

void DinoActivity::onEnter() {
    Activity::onEnter();

    screenW_ = renderer.getScreenWidth();
    screenH_ = renderer.getScreenHeight();
    groundY_ = screenH_ - GROUND_MARGIN;

    randomSeed(millis());
    resetGame();

    renderPending_ = true;
    requestUpdate();
}

void DinoActivity::onExit() {
    Activity::onExit();
}

void DinoActivity::resetGame() {
    dinoY_     = 0;
    velocityY_ = 0;
    jumping_   = false;

    for (auto& ob : obstacles_) {
        ob.active = false;
    }
    distanceSinceSpawn_ = 0;
    nextSpawnDistance_  = START_SPEED * (float)random(MIN_GAP_TICKS, MAX_GAP_TICKS);

    birdX_ = screenW_ + random(0, 200);
    birdY_ = random(BIRD_TOP_MIN, BIRD_TOP_MAX);

    gameSpeed_ = START_SPEED;
    score_     = 0;
    gameOver_  = false;
    animFrame_ = false;

    lastUpdate_    = millis();
    lastAnim_      = lastUpdate_;
    lastScoreTick_ = lastUpdate_;
}

int DinoActivity::obstacleWidthPx(ObstacleType type) const {
    switch (type) {
        case ObstacleType::CactusSmall: return CACTUS_SMALL_W * BLOCK;
        case ObstacleType::CactusBig:   return CACTUS_BIG_W * BLOCK;
    }
    return 0;
}

int DinoActivity::obstacleHeightPx(ObstacleType type) const {
    switch (type) {
        case ObstacleType::CactusSmall: return CACTUS_SMALL_H * BLOCK;
        case ObstacleType::CactusBig:   return CACTUS_BIG_H * BLOCK;
    }
    return 0;
}

void DinoActivity::spawnObstacle() {
    int slot = -1;
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!obstacles_[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return;

    ObstacleType type = (random(0, 100) < 55) ? ObstacleType::CactusSmall : ObstacleType::CactusBig;

    obstacles_[slot].active = true;
    obstacles_[slot].type   = type;
    obstacles_[slot].x      = screenW_;
}

bool DinoActivity::anyNonPowerPressed() const {
    return mappedInput.wasPressed(MappedInputManager::Button::Back) ||
           mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
           mappedInput.wasPressed(MappedInputManager::Button::Left) ||
           mappedInput.wasPressed(MappedInputManager::Button::Right) ||
           mappedInput.wasPressed(MappedInputManager::Button::Up) ||
           mappedInput.wasPressed(MappedInputManager::Button::Down);
}

bool DinoActivity::checkCollision() const {
    constexpr int INSET = 4;
    constexpr int FRONT_INSET = 10;
    constexpr int OBSTACLE_INSET_X = 10;

    int dw  = (DINO_GRID_W - DINO_COLLIDE_X_OFFSET) * BLOCK;
    int dh  = DINO_GRID_H * BLOCK;
    int dx0 = DINO_X + DINO_COLLIDE_X_OFFSET * BLOCK + INSET;
    int dx1 = DINO_X + DINO_COLLIDE_X_OFFSET * BLOCK + dw - FRONT_INSET;
    int dy0 = groundY_ - dh + (int)dinoY_ + INSET;
    int dy1 = groundY_ + (int)dinoY_ - INSET;

    for (const auto& ob : obstacles_) {
        if (!ob.active) continue;

        int ow  = obstacleWidthPx(ob.type);
        int oh  = obstacleHeightPx(ob.type);
        int oy0 = groundY_ - oh;
        int oy1 = oy0 + oh;
        int ox0 = (int)ob.x + OBSTACLE_INSET_X;
        int ox1 = (int)ob.x + ow - OBSTACLE_INSET_X;

        if (dx0 < ox1 && dx1 > ox0 && dy0 < oy1 && dy1 > oy0) {
            return true;
        }
    }
    return false;
}

void DinoActivity::updateGame() {
    for (auto& ob : obstacles_) {
        if (!ob.active) continue;
        ob.x -= gameSpeed_;
        if (ob.x + obstacleWidthPx(ob.type) < 0) {
            ob.active = false;
        }
    }

    distanceSinceSpawn_ += gameSpeed_;
    if (distanceSinceSpawn_ >= nextSpawnDistance_) {
        spawnObstacle();
        distanceSinceSpawn_ = 0;
        nextSpawnDistance_  = gameSpeed_ * (float)random(MIN_GAP_TICKS, MAX_GAP_TICKS);
    }

    birdX_ -= BIRD_SPEED;
    if (birdX_ + BIRD_W * BLOCK < 0) {
        birdX_ = screenW_ + random(0, 200);
        birdY_ = random(BIRD_TOP_MIN, BIRD_TOP_MAX);
    }

    if (jumping_) {
        velocityY_ += GRAVITY;
        dinoY_     += velocityY_;
        if (dinoY_ >= 0) {
            dinoY_     = 0;
            velocityY_ = 0;
            jumping_   = false;
        }
    }

    gameSpeed_ = START_SPEED + SPEED_STEP * (float)(score_ / SCORE_PER_SPEEDUP);
    if (gameSpeed_ > MAX_SPEED) gameSpeed_ = MAX_SPEED;

    if (checkCollision()) {
        gameOver_ = true;
    }
}

void DinoActivity::loop() {
    if (gameOver_) {
        if (mappedInput.wasPressed(MappedInputManager::Button::Power)) {
            while (mappedInput.isPressed(MappedInputManager::Button::Power)) {
                mappedInput.update();
                delay(10);
            }
            finish();
            return;
        }
        if (anyNonPowerPressed()) {
            resetGame();
            renderPending_ = true;
            requestUpdate();
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

    if (anyNonPowerPressed() && !jumping_) {
        jumping_   = true;
        velocityY_ = JUMP_VELOCITY;
    }

    unsigned long now = millis();

    if (now - lastUpdate_ >= PHYSICS_INTERVAL_MS) {
        lastUpdate_ = now;
        updateGame();
        renderPending_ = true;
    }

    if (now - lastAnim_ >= ANIM_INTERVAL_MS) {
        lastAnim_  = now;
        animFrame_ = !animFrame_;
        renderPending_ = true;
    }

    if (now - lastScoreTick_ >= SCORE_INTERVAL_MS) {
        lastScoreTick_ = now;
        score_++;
        renderPending_ = true;
    }

    if (renderPending_) {
        requestUpdate();
    }
}

void DinoActivity::drawDino(int x, int topY) const {
    drawSprite(renderer, DINO_HEAD_BODY, lengthOf(DINO_HEAD_BODY), x, topY, BLOCK);

    const SpriteRow* legSpans;
    size_t legCount;
    if (jumping_) {
        legSpans = DINO_JUMP_LEGS;
        legCount = lengthOf(DINO_JUMP_LEGS);
    } else {
        legSpans = animFrame_ ? DINO_RUN2_LEGS : DINO_RUN1_LEGS;
        legCount = animFrame_ ? lengthOf(DINO_RUN2_LEGS) : lengthOf(DINO_RUN1_LEGS);
    }
    drawSprite(renderer, legSpans, legCount, x, topY, BLOCK);

    constexpr int EYE_ROW = 2, EYE_C0 = 15, EYE_C1 = 16, EYE_ROWS = 2;
    renderer.fillRect(x + EYE_C0 * BLOCK, topY + EYE_ROW * BLOCK,
                       (EYE_C1 - EYE_C0 + 1) * BLOCK, EYE_ROWS * BLOCK, false);
}

void DinoActivity::drawObstacle(const Obstacle& ob) const {
    int h = obstacleHeightPx(ob.type);
    int x = (int)ob.x;
    int y = groundY_ - h;

    switch (ob.type) {
        case ObstacleType::CactusSmall:
            drawSprite(renderer, CACTUS_SMALL, lengthOf(CACTUS_SMALL), x, y, BLOCK);
            break;
        case ObstacleType::CactusBig:
            drawSprite(renderer, CACTUS_BIG, lengthOf(CACTUS_BIG), x, y, BLOCK);
            break;
    }
}

void DinoActivity::drawBird() const {
    if (animFrame_) {
        drawSprite(renderer, BIRD_UP, lengthOf(BIRD_UP), (int)birdX_, birdY_, BLOCK);
    } else {
        drawSprite(renderer, BIRD_DOWN, lengthOf(BIRD_DOWN), (int)birdX_, birdY_, BLOCK);
    }
}

void DinoActivity::render(RenderLock&&) {
    renderPending_ = false;

    renderer.clearScreen();

    renderer.drawLine(0, groundY_, screenW_, groundY_);

    drawBird();

    char scoreStr[16];
    snprintf(scoreStr, sizeof(scoreStr), "%05lu", score_);
    int scoreW = renderer.getTextWidth(UI_10_FONT_ID, scoreStr);
    renderer.drawText(UI_10_FONT_ID, screenW_ - scoreW - 24, 24, scoreStr, true);

    int dinoTopY = groundY_ - DINO_GRID_H * BLOCK + (int)dinoY_;
    drawDino(DINO_X, dinoTopY);

    for (const auto& ob : obstacles_) {
        if (ob.active) drawObstacle(ob);
    }

    if (gameOver_) {
        const char* line1 = "GAME OVER";
        const char* line2 = "Any button to Restart";
        const char* line3 = "Power button to Exit";

        int w1 = renderer.getTextWidth(UI_12_FONT_ID, line1, EpdFontFamily::BOLD);
        int w2 = renderer.getTextWidth(UI_10_FONT_ID, line2);
        int w3 = renderer.getTextWidth(UI_10_FONT_ID, line3);

        int baseY = groundY_ / 2 - 20;
        renderer.drawText(UI_12_FONT_ID, screenW_ / 2 - w1 / 2, baseY,
                           line1, true, EpdFontFamily::BOLD);
        renderer.drawText(UI_10_FONT_ID, screenW_ / 2 - w2 / 2, baseY + 36, line2, true);
        renderer.drawText(UI_10_FONT_ID, screenW_ / 2 - w3 / 2, baseY + 62, line3, true);
    }

    renderer.displayBuffer();
}
