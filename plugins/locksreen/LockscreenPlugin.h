#pragma once

#include <Arduino.h>

enum class LockscreenMode : uint8_t {
    MODE_OFF = 0,
    ENABLED  = 1,
};

namespace LockscreenPlugin {

const char* modeName(LockscreenMode mode);
bool shouldLock(LockscreenMode mode);
bool shouldLockOnWake(LockscreenMode mode);
bool shouldLockOnPower(LockscreenMode mode);
void hashPin(const char* pin, char outHash[9]);
bool checkPin(const char* pin, const char* storedHash);

}
