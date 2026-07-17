#pragma once

#include "activities/Activity.h"

class DinoActivity final : public Activity {
public:
    explicit DinoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
        : Activity("Dino", renderer, mappedInput) {}

    void onEnter() override;
    void onExit() override;
    void loop() override;
    bool skipLoopDelay() override { return true; }
    void render(RenderLock&&) override;

private:
    enum class ObstacleType { CactusSmall, CactusBig };

    struct Obstacle {
        bool active = false;
        ObstacleType type = ObstacleType::CactusSmall;
        float x = 0;
    };

    static constexpr int BLOCK             = 5;
    static constexpr int GROUND_MARGIN     = 60;
    static constexpr int DINO_X            = 36;
    static constexpr float GRAVITY         = 0.5f;
    static constexpr float JUMP_VELOCITY   = -16.0f;
    static constexpr int MAX_OBSTACLES     = 3;
    static constexpr float START_SPEED     = 4.2f;
    static constexpr float MAX_SPEED       = 11.0f;
    static constexpr float SPEED_STEP      = 0.35f;
    static constexpr int SCORE_PER_SPEEDUP = 150;
    static constexpr int MIN_GAP_TICKS     = 100;
    static constexpr int MAX_GAP_TICKS     = 160;
    static constexpr float BIRD_SPEED      = 2.0f;
    static constexpr int BIRD_TOP_MIN      = 40;
    static constexpr int BIRD_TOP_MAX      = 140;
    static constexpr unsigned long PHYSICS_INTERVAL_MS = 33;
    static constexpr unsigned long ANIM_INTERVAL_MS     = 130;
    static constexpr unsigned long SCORE_INTERVAL_MS    = 90;

    int screenW_ = 0;
    int screenH_ = 0;
    int groundY_ = 0;

    float dinoY_     = 0;
    float velocityY_ = 0;
    bool jumping_     = false;

    Obstacle obstacles_[MAX_OBSTACLES];
    float distanceSinceSpawn_ = 0;
    float nextSpawnDistance_  = 0;

    float birdX_ = 0;
    int birdY_   = 0;

    float gameSpeed_    = START_SPEED;
    unsigned long score_ = 0;

    bool gameOver_      = false;
    bool renderPending_ = false;
    bool animFrame_     = false;

    unsigned long lastUpdate_    = 0;
    unsigned long lastAnim_      = 0;
    unsigned long lastScoreTick_ = 0;

    void resetGame();
    void updateGame();
    void spawnObstacle();
    bool checkCollision() const;
    bool anyNonPowerPressed() const;
    void drawDino(int x, int topY) const;
    void drawObstacle(const Obstacle& ob) const;
    void drawBird() const;
    int obstacleWidthPx(ObstacleType type) const;
    int obstacleHeightPx(ObstacleType type) const;
};
