#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "AudiobookshelfParse.h"

struct AbsConfig {
  std::string server;
  std::string user;
  std::string pass;
  std::string token;
  std::string library;
  bool savePass = false;
};

enum class AbsResult {
  OK,
  NO_WIFI,
  LOW_MEMORY,
  AUTH_ERROR,
  NOT_FOUND,
  NO_LIBRARY,
  SERVER_ERROR,
  NETWORK_ERROR,
  PARSE_ERROR,
  SD_ERROR,
  NOT_EPUB
};

using AbsProgress = std::function<void(size_t done, size_t total)>;

namespace AudiobookshelfPlugin {

void loadConfig(AbsConfig& cfg);
void saveConfig(const AbsConfig& cfg);

AbsResult login(AbsConfig& cfg);
AbsResult authorize(AbsConfig& cfg);
AbsResult loadShelves(const AbsConfig& cfg, std::vector<AbsShelf>& out);
AbsResult search(const AbsConfig& cfg, const std::string& query, std::vector<AbsItem>& out);
AbsResult downloadBook(const AbsConfig& cfg, const std::string& itemId, const std::string& destPath,
                       const AbsProgress& progress);

std::string bookPath(const AbsItem& item);
const char* message(AbsResult r);

}
