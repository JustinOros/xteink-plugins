#include "LockscreenActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/settings/LockscreenPlugin.h"
#include "components/UITheme.h"
#include "fontIds.h"

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

void LockscreenActivity::submitPin() {
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
}

void LockscreenActivity::pressCurrentKey() {
    const char* key = KEYS[cursorRow_][cursorCol_];

    if (key[0] == '\0') return;

    if (strcmp(key, "<") == 0) {
        if (pinLen_ > 0) {
            pinLen_--;
            pin_[pinLen_] = '\0';
        }
        renderPending_ = true;
        requestUpdate();
        return;
    }

    if (pinLen_ < PIN_LEN) {
        pin_[pinLen_++] = key[0];
        pin_[pinLen_]   = '\0';
        renderPending_ = true;
        requestUpdate();
        if (pinLen_ == PIN_LEN) {
            submitPin();
        }
    }
}

void LockscreenActivity::moveCursor(int dRow, int dCol) {
    for (int i = 0; i < ROWS * COLS; i++) {
        cursorRow_ = (cursorRow_ + dRow + ROWS) % ROWS;
        cursorCol_ = (cursorCol_ + dCol + COLS) % COLS;
        if (KEYS[cursorRow_][cursorCol_][0] != '\0') return;
    }
}

void LockscreenActivity::renderDots(int centerX, int centerY) const {
    constexpr int DOT = 26;
    constexpr int GAP = 34;
    const int total  = PIN_LEN * DOT + (PIN_LEN - 1) * GAP;
    const int startX = centerX - total / 2;
    for (int i = 0; i < PIN_LEN; i++) {
        const int x = startX + i * (DOT + GAP);
        const int y = centerY - DOT / 2;
        if (i < pinLen_) {
            renderer.fillRoundedRect(x, y, DOT, DOT, DOT / 2, Color::Black);
        } else {
            renderer.drawRoundedRect(x, y, DOT, DOT, 2, DOT / 2, true);
        }
    }
}

void LockscreenActivity::renderBackspaceIcon(int cx, int cy, bool dark) const {
    constexpr int W = 46;
    constexpr int H = 32;
    const int x0 = cx - W / 2;
    const int top = cy - H / 2;
    const int bottom = cy + H / 2;
    const int tip = x0 + H / 2;
    renderer.drawLine(x0, cy, tip, top, 2, dark);
    renderer.drawLine(tip, top, x0 + W, top, 2, dark);
    renderer.drawLine(x0 + W, top, x0 + W, bottom, 2, dark);
    renderer.drawLine(x0 + W, bottom, tip, bottom, 2, dark);
    renderer.drawLine(tip, bottom, x0, cy, 2, dark);
    const int mx = x0 + (W + H / 2) / 2;
    renderer.drawLine(mx - 6, cy - 6, mx + 6, cy + 6, 2, dark);
    renderer.drawLine(mx - 6, cy + 6, mx + 6, cy - 6, 2, dark);
}

void LockscreenActivity::renderKeypad(int top, int bottom) const {
    const int screenW = renderer.getScreenWidth();
    constexpr int SIDE = 30;
    const int colW = (screenW - 2 * SIDE) / COLS;
    const int rowH = (bottom - top) / ROWS;
    const int half = (colW < rowH ? colW : rowH) / 2 - 6;
    const int lineH = renderer.getLineHeight(UI_12_FONT_ID);

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            const char* label = KEYS[r][c];
            if (label[0] == '\0') continue;

            const int cx = SIDE + c * colW + colW / 2;
            const int cy = top + r * rowH + rowH / 2;
            const bool isCursor = (r == cursorRow_ && c == cursorCol_);

            if (isCursor) {
                renderer.fillRoundedRect(cx - half, cy - half, half * 2, half * 2, 14, Color::Black);
            }

            if (strcmp(label, "<") == 0) {
                renderBackspaceIcon(cx, cy, !isCursor);
            } else {
                const int w = renderer.getTextWidth(UI_12_FONT_ID, label, EpdFontFamily::BOLD);
                renderer.drawText(UI_12_FONT_ID, cx - w / 2, cy - lineH / 2, label, !isCursor, EpdFontFamily::BOLD);
            }
        }
    }
}

void LockscreenActivity::render(RenderLock&&) {
    const int screenW = renderer.getScreenWidth();
    const int screenH = renderer.getScreenHeight();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int usableH = screenH - metrics.buttonHintsHeight;

    renderer.clearScreen();

    const char* title = (purpose_ == Purpose::CREATE) ? "Set passcode" : "Enter passcode";
    const int titleW = renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, (screenW - titleW) / 2, usableH * 9 / 100, title, true, EpdFontFamily::BOLD);

    const int dotsY = usableH * 21 / 100;
    renderDots(screenW / 2, dotsY);

    if (attempts_ > 0) {
        char msg[48];
        snprintf(msg, sizeof(msg), "Incorrect PIN. %d attempt(s) remaining.", MAX_ATTEMPTS - attempts_);
        renderer.drawCenteredText(UI_10_FONT_ID, dotsY + 40, msg);
    }

    renderKeypad(usableH * 33 / 100, usableH - 20);

    GUI.drawButtonHints(renderer, "Delete", "Select", "Left", "Right");
    GUI.drawSideButtonHints(renderer, "Up", "Down");

    {
        constexpr int buttonWidth  = 30;
        constexpr int buttonHeight = 60;
        constexpr int powerButtonY = 135;
        const int x = screenW - buttonWidth;
        renderer.drawRoundedRect(x, powerButtonY, buttonWidth, buttonHeight, 1, 6, true, false, true, false, true);
        const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, "Select");
        renderer.drawTextRotated90CW(SMALL_FONT_ID, x, powerButtonY + (buttonHeight + textWidth) / 2, "Select");
    }

    renderer.displayBuffer();
}

bool LockscreenActivity::keyAt(int x, int y, int& row, int& col) const {
    constexpr int SIDE = 30;
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int usableH = renderer.getScreenHeight() - metrics.buttonHintsHeight;
    const int top = usableH * 33 / 100;
    const int bottom = usableH - 20;
    const int colW = (renderer.getScreenWidth() - 2 * SIDE) / COLS;
    const int rowH = (bottom - top) / ROWS;
    if (colW <= 0 || rowH <= 0 || x < SIDE || y < top) return false;
    const int c = (x - SIDE) / colW;
    const int r = (y - top) / rowH;
    if (c >= COLS || r >= ROWS) return false;
    row = r;
    col = c;
    return true;
}

void LockscreenActivity::loop() {
    if (purpose_ == Purpose::UNLOCK) {
        if (millis() - enterTime_ > TIMEOUT_MS) {
            goToSleep();
            return;
        }
    }

    int touchX = 0;
    int touchY = 0;
    int touchRow = 0;
    int touchCol = 0;
    if (mappedInput.wasScreenTapped(touchX, touchY)) {
        if (keyAt(touchX, touchY, touchRow, touchCol) && KEYS[touchRow][touchCol][0] != '\0') {
            cursorRow_ = touchRow;
            cursorCol_ = touchCol;
            pressCurrentKey();
        }
        return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        moveCursor(-1, 0);
        renderPending_ = true;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        moveCursor(1, 0);
        renderPending_ = true;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        moveCursor(0, -1);
        renderPending_ = true;
        requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        moveCursor(0, 1);
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
