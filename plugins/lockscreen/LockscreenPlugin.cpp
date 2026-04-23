#include "LockscreenPlugin.h"

namespace LockscreenPlugin {

const char* modeName(LockscreenMode mode) {
    switch (mode) {
        case LockscreenMode::ENABLED: return "Enabled";
        default:                      return "Disabled";
    }
}

bool shouldLock(LockscreenMode mode) {
    return mode == LockscreenMode::ENABLED;
}

bool shouldLockOnWake(LockscreenMode mode) {
    return shouldLock(mode);
}

bool shouldLockOnPower(LockscreenMode mode) {
    return shouldLock(mode);
}

void hashPin(const char* pin, char outHash[9]) {
    uint32_t hash = 5381;
    for (const char* p = pin; *p; p++) {
        hash = ((hash << 5) + hash) ^ static_cast<uint8_t>(*p);
    }
    hash ^= 0xA3F1C2B4u;
    snprintf(outHash, 9, "%08X", hash);
}

bool checkPin(const char* pin, const char* storedHash) {
    if (!pin || !storedHash || storedHash[0] == '\0') return false;
    char computed[9];
    hashPin(pin, computed);
    return strncmp(computed, storedHash, 9) == 0;
}

}
