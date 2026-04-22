#pragma once

#include <Arduino.h>

namespace GitHubSyncPlugin {

    enum class SyncResult {
        OK,
        NO_WIFI,
        GIT_ERROR,
    };

    SyncResult sync();

}
