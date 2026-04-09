#pragma once

#include <Arduino.h>

namespace HardcoverPlugin {

    enum class SyncResult {
        OK,           // All books synced successfully
        NO_BOOKS,     // No in-progress epub books with ISBN found
        NO_TOKEN,     // API token not configured
        NO_WIFI,      // Not connected to WiFi
        API_ERROR,    // HTTP or GraphQL error from Hardcover
    };

    SyncResult syncProgress();
}
