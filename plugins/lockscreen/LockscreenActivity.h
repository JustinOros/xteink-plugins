#pragma once

#include "activities/settings/LockscreenPlugin.h"
#include "activities/Activity.h"

class LockscreenActivity final : public Activity {
 public:
    enum class Purpose { UNLOCK, CREATE };

    static constexpr int COLS = 3;
    static constexpr int ROWS = 4;

    explicit LockscreenActivity(GfxRenderer& renderer,
                                MappedInputManager& mappedInput,
                                Purpose purpose)
        : Activity("Lockscreen", renderer, mappedInput), purpose_(purpose) {}

    void onEnter() override;
    void onExit() override;
    void loop() override;
    bool skipLoopDelay() override { return true; }
    void render(RenderLock&&) override;

    bool wasSuccessful() const { return success_; }
    bool isDone() const { return done_; }
    bool needsRender() const { return renderPending_; }

    void renderDirect() {
        renderPending_ = false;
        RenderLock rl(*this);
        render(std::move(rl));
    }

 private:
    static constexpr int PIN_LEN = 4;
    static constexpr unsigned long TIMEOUT_MS = 120000;

    static constexpr const char* KEYS[ROWS][COLS] = {
        {"1", "2", "3"},
        {"4", "5", "6"},
        {"7", "8", "9"},
        {"", "0", "<"},
    };

    Purpose purpose_;
    bool    success_       = false;
    bool    done_          = false;
    bool    renderPending_ = false;

    int cursorRow_ = 0;
    int cursorCol_ = 0;

    char pin_[PIN_LEN + 1] = {};
    int  pinLen_            = 0;

    int attempts_ = 0;
    static constexpr int MAX_ATTEMPTS = 3;

    unsigned long enterTime_ = 0;

    void pressCurrentKey();
    void submitPin();
    void moveCursor(int dRow, int dCol);
    bool keyAt(int x, int y, int& row, int& col) const;
    void renderDots(int centerX, int centerY) const;
    void renderBackspaceIcon(int cx, int cy, bool dark) const;
    void renderKeypad(int top, int bottom) const;
    void goToSleep();
};
