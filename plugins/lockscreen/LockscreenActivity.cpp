#include "LockscreenActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/settings/LockscreenPlugin.h"
#include "components/UITheme.h"
#include "fontIds.h"

static constexpr int CARD_MARGIN = 30;

static constexpr int KEY_W     = 110;
static constexpr int KEY_H     = 72;
static constexpr int KEY_PAD_X = 14;
static constexpr int KEY_PAD_Y = 10;

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
                while (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
                    mappedInput.update();
                    delay(10);
                }
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
        renderPending_ = true;
        requestUpdate();
    }
}

void LockscreenActivity::renderPinDisplay(int inputX, int inputY, int inputLineY, int inputW, bool masked) const {
    char dotStr[PIN_LEN * 4 + 2] = {};
    int pos = 0;
    for (int i = 0; i < pinLen_; i++) {
        if (masked) {
            dotStr[pos++] = '\xe2';
            dotStr[pos++] = '\x80';
            dotStr[pos++] = '\xa2';
        } else {
            dotStr[pos++] = pin_[i];
        }
    }
    dotStr[pos] = '\0';

    renderer.drawText(UI_12_FONT_ID, inputX, inputY, dotStr, true, EpdFontFamily::BOLD);
    renderer.drawLine(inputX, inputLineY, inputX + inputW, inputLineY);
}

void LockscreenActivity::renderKeypad(int gridX, int gridY) const {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int kx = gridX + c * (KEY_W + KEY_PAD_X);
            int ky = gridY + r * (KEY_H + KEY_PAD_Y);
            bool isCursor = (r == cursorRow_ && c == cursorCol_);

            const char* displayLabel = KEYS[r][c];

            if (isCursor) {
                constexpr int HL_PAD = 8;
                renderer.fillRect(kx - HL_PAD, ky - HL_PAD / 2,
                                  KEY_W + HL_PAD * 2, KEY_H + HL_PAD, true);
                renderer.drawText(UI_12_FONT_ID,
                                  kx + KEY_W / 2 - 8,
                                  ky + KEY_H / 2 - 10,
                                  displayLabel, false, EpdFontFamily::BOLD);
            } else {
                renderer.drawText(UI_12_FONT_ID,
                                  kx + KEY_W / 2 - 8,
                                  ky + KEY_H / 2 - 10,
                                  displayLabel, true, EpdFontFamily::REGULAR);
            }
        }
    }
}

void LockscreenActivity::render(RenderLock&&) {
    const bool isCreate = (purpose_ == Purpose::CREATE);
    const bool masked   = !isCreate;

    const int screenW = renderer.getScreenWidth();
    const int screenH = renderer.getScreenHeight();

    const int cardX = CARD_MARGIN;
    const int cardY = CARD_MARGIN;
    const int cardW = screenW - 2 * CARD_MARGIN;
    const int cardH = screenH - 2 * CARD_MARGIN;

    const int titleY     = cardY + 36;
    const int inputY     = cardY + 110;
    const int inputLineY = inputY + 42;
    const int inputX     = cardX + 30;
    const int inputW     = cardW - 60;

    const int gridW = COLS * KEY_W + (COLS - 1) * KEY_PAD_X;
    const int gridX = (screenW - gridW) / 2;
    const int gridY = inputLineY + 60;

    renderer.clearScreen();
    renderer.drawRect(cardX, cardY, cardW, cardH);

    const char* title = isCreate ? "Set Passcode" : "Enter Passcode";
    renderer.drawText(UI_12_FONT_ID, cardX + 30, titleY, title, true, EpdFontFamily::BOLD);

    renderPinDisplay(inputX, inputY, inputLineY, inputW, masked);
    renderKeypad(gridX, gridY);

    if (attempts_ > 0) {
        char msg[48];
        snprintf(msg, sizeof(msg), "Incorrect PIN. %d attempt(s) remaining.", MAX_ATTEMPTS - attempts_);
        renderer.drawCenteredText(UI_10_FONT_ID, cardY + cardH - 28, msg);
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
