#include "LockscreenActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/settings/LockscreenPlugin.h"
#include "components/UITheme.h"
#include "fontIds.h"

static constexpr int SCREEN_W = 480;
static constexpr int KEY_W    = 130;
static constexpr int KEY_H    = 80;
static constexpr int KEY_PAD  = 8;
static constexpr int GRID_W   = LockscreenActivity::COLS * (KEY_W + KEY_PAD) - KEY_PAD;
static constexpr int GRID_X   = (SCREEN_W - GRID_W) / 2;

void LockscreenActivity::onEnter() {
    Activity::onEnter();
    success_       = false;
    done_          = false;
    renderPending_ = true;
    pinLen_        = 0;
    attempts_      = 0;
    cursorRow_     = 0;
    cursorCol_     = 0;
    memset(pin_, 0, sizeof(pin_));
    enterTime_     = millis();
    requestUpdate();
}

void LockscreenActivity::onExit() {
    Activity::onExit();
}

void LockscreenActivity::goToSleep() {
    done_    = true;
    success_ = false;
}

void LockscreenActivity::pressCurrentKey() {
    const char* key = KEYS[cursorRow_][cursorCol_];

    if (strcmp(key, "<") == 0) {
        if (pinLen_ > 0) {
            pinLen_--;
            pin_[pinLen_] = '\0';
        }
        renderPending_ = true;
        requestUpdate();
        return;
    }

    if (strcmp(key, "OK") == 0) {
        if (pinLen_ < PIN_LEN) return;

        if (purpose_ == Purpose::CREATE) {
            LockscreenPlugin::hashPin(pin_, SETTINGS.lockscreenPinHash);
            SETTINGS.saveToFile();
            success_ = true;
            done_    = true;
            finish();
        } else {
            if (LockscreenPlugin::checkPin(pin_, SETTINGS.lockscreenPinHash)) {
                success_ = true;
                done_    = true;
            } else {
                attempts_++;
                pinLen_ = 0;
                memset(pin_, 0, sizeof(pin_));
                renderPending_ = true;
                requestUpdate();
                if (attempts_ >= MAX_ATTEMPTS) {
                    goToSleep();
                }
            }
        }
        return;
    }

    if (pinLen_ < PIN_LEN) {
        pin_[pinLen_++] = key[0];
        pin_[pinLen_]   = '\0';
        if (pinLen_ == PIN_LEN) {
            cursorRow_ = 3;
            cursorCol_ = 2;
        }
        renderPending_  = true;
        requestUpdate();
    }
}

void LockscreenActivity::renderPinDisplay(int startY, bool masked) const {
    constexpr int BOX_W   = 50;
    constexpr int BOX_H   = 60;
    constexpr int BOX_PAD = 12;
    constexpr int TOTAL_W = 4 * BOX_W + 3 * BOX_PAD;
    int x = (SCREEN_W - TOTAL_W) / 2;

    for (int i = 0; i < PIN_LEN; i++) {
        int bx = x + i * (BOX_W + BOX_PAD);
        renderer.drawRect(bx, startY, BOX_W, BOX_H);
        if (i < pinLen_) {
            char display[2] = {masked ? '*' : pin_[i], '\0'};
            renderer.drawText(UI_12_FONT_ID, bx + BOX_W / 2 - 8, startY + BOX_H / 2 - 10, display);
        }
    }
}

void LockscreenActivity::renderKeypad(int startY) const {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int kx = GRID_X + c * (KEY_W + KEY_PAD);
            int ky = startY + r * (KEY_H + KEY_PAD);
            bool isCursor = (r == cursorRow_ && c == cursorCol_);
            if (isCursor) {
                renderer.fillRect(kx, ky, KEY_W, KEY_H);
                renderer.drawText(UI_12_FONT_ID, kx + KEY_W / 2 - 8, ky + KEY_H / 2 - 10,
                                  KEYS[r][c], false, EpdFontFamily::BOLD);
            } else {
                renderer.drawRect(kx, ky, KEY_W, KEY_H);
                renderer.drawText(UI_12_FONT_ID, kx + KEY_W / 2 - 8, ky + KEY_H / 2 - 10,
                                  KEYS[r][c], true, EpdFontFamily::REGULAR);
            }
        }
    }
}

void LockscreenActivity::render(RenderLock&&) {
    const auto& metrics  = UITheme::getInstance().getMetrics();
    const auto  pageH    = renderer.getScreenHeight();
    const bool  isCreate = (purpose_ == Purpose::CREATE);
    const bool  masked   = !isCreate;

    renderer.clearScreen();

    const char* title = isCreate ? "Set PIN" : "Unlock";
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, SCREEN_W, metrics.headerHeight}, title);

    int y = metrics.topPadding + metrics.headerHeight + 80;

    renderPinDisplay(y, masked);
    y += 90;
    renderKeypad(y);

    if (isCreate) {
        renderer.drawCenteredText(UI_10_FONT_ID, pageH - 40, "Navigate: Directional buttons  |  Select: Power");
    } else if (attempts_ > 0) {
        char msg[40];
        snprintf(msg, sizeof(msg), "Incorrect PIN. %d attempt(s) remaining.", MAX_ATTEMPTS - attempts_);
        renderer.drawCenteredText(UI_10_FONT_ID, pageH - 40, msg);
    }

    renderer.displayBuffer();
}

void LockscreenActivity::loop() {
    if (purpose_ == Purpose::UNLOCK) {
        if (millis() - enterTime_ > TIMEOUT_MS) {
            goToSleep();
            return;
        }
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        cursorRow_     = (cursorRow_ - 1 + ROWS) % ROWS;
        renderPending_ = true;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        cursorRow_     = (cursorRow_ + 1) % ROWS;
        renderPending_ = true;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        cursorCol_     = (cursorCol_ - 1 + COLS) % COLS;
        renderPending_ = true;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        cursorCol_     = (cursorCol_ + 1) % COLS;
        renderPending_ = true;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Power) ||
               mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        pressCurrentKey();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        if (pinLen_ > 0) {
            pinLen_--;
            pin_[pinLen_] = '\0';
            renderPending_ = true;
            requestUpdate();
        }
    }
}
