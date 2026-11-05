#include "AudiobookshelfPlugin.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <WiFi.h>

#include "CrossPointSettings.h"

namespace {

constexpr const char* TMP_JSON = "/.crosspoint/abs_tmp.json";
constexpr const char* TMP_DIR = "/.crosspoint";
constexpr uint32_t MIN_FREE_HEAP = 45000;
constexpr uint32_t HTTP_TIMEOUT_MS = 30000;
constexpr int MAX_REDIRECTS = 4;
constexpr size_t MAX_SMALL_BODY = 16384;
constexpr int SHELF_LIMIT = 10;

std::string g_libraryId;

void setField(char* field, size_t size, const std::string& value) {
  strncpy(field, value.c_str(), size - 1);
  field[size - 1] = '\0';
}

struct FileReader {
  HalFile& file;
  int read() { return file.read(); }
  size_t readBytes(char* buf, size_t len) {
    const int n = file.read(buf, len);
    return n > 0 ? static_cast<size_t>(n) : 0;
  }
};

AbsResult mapStatus(int status) {
  if (status < 0) return AbsResult::NETWORK_ERROR;
  if (status == 401 || status == 403) return AbsResult::AUTH_ERROR;
  if (status == 404) return AbsResult::NOT_FOUND;
  return AbsResult::SERVER_ERROR;
}

using BodyCallback = std::function<bool(const uint8_t* data, size_t len, size_t total)>;

AbsResult http(const char* method, const std::string& startUrl, const std::string& token, const std::string& body,
               const BodyCallback& onData, int& statusOut) {
  statusOut = -1;
  if (WiFi.status() != WL_CONNECTED) return AbsResult::NO_WIFI;
  if (ESP.getFreeHeap() < MIN_FREE_HEAP) return AbsResult::LOW_MEMORY;

  const std::string origin = AbsParse::origin(startUrl);
  std::string url = startUrl;
  const bool isPost = body.size() > 0;

  for (int hop = 0; hop <= MAX_REDIRECTS; ++hop) {
    freeink::SecureHttpClient client;
    client.setTimeout(HTTP_TIMEOUT_MS);
    client.setInsecure();
    if (!client.begin(url)) return AbsResult::NETWORK_ERROR;
    client.addHeader("User-Agent", "CrossPoint-Audiobookshelf");
    client.addHeader("Accept", "application/json, application/epub+zip, */*");
    if (isPost) client.addHeader("Content-Type", "application/json");
    if (!token.empty() && AbsParse::origin(url) == origin) client.addHeader("Authorization", "Bearer " + token);

    const int status = client.sendRequest(
        method, reinterpret_cast<const uint8_t*>(body.data()), body.size(),
        [&client, &onData](const uint8_t* data, size_t len) {
          if (client.getStatus() < 200 || client.getStatus() > 299) return true;
          return onData(data, len, client.hasContentLength() ? client.getContentLength() : 0);
        });
    statusOut = status;

    if (status < 0) return AbsResult::NETWORK_ERROR;
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      std::string next;
      if (isPost || !freeink::SecureHttpClient::resolveUrl(url, client.getHeader("location"), next)) {
        return AbsResult::SERVER_ERROR;
      }
      url = next;
      continue;
    }
    if (status < 200 || status > 299) return mapStatus(status);
    if (client.callbackAborted()) return AbsResult::SD_ERROR;
    if (!client.responseComplete()) return AbsResult::NETWORK_ERROR;
    return AbsResult::OK;
  }
  return AbsResult::SERVER_ERROR;
}

AbsResult httpToString(const char* method, const std::string& url, const std::string& token, const std::string& body,
                       std::string& out) {
  out.clear();
  int status = 0;
  return http(method, url, token, body, [&out](const uint8_t* data, size_t len, size_t) {
    if (out.size() + len > MAX_SMALL_BODY) return false;
    out.append(reinterpret_cast<const char*>(data), len);
    return true;
  }, status);
}

AbsResult httpToFile(const std::string& url, const std::string& token, const std::string& path, bool checkEpub,
                     const AbsProgress& progress) {
  auto& sd = Storage;
  sd.mkdir(TMP_DIR);
  if (sd.exists(path.c_str())) sd.remove(path.c_str());
  HalFile file;
  if (!sd.openFileForWrite("ABS", path.c_str(), file)) return AbsResult::SD_ERROR;

  size_t written = 0;
  bool notEpub = false;
  uint8_t magic[2] = {0, 0};
  int status = 0;
  const AbsResult r = http("GET", url, token, "", [&](const uint8_t* data, size_t len, size_t total) {
    if (checkEpub) {
      for (size_t i = 0; i < len && written + i < 2; ++i) magic[written + i] = data[i];
      if (written + len >= 2 && (magic[0] != 'P' || magic[1] != 'K')) {
        notEpub = true;
        return false;
      }
    }
    if (file.write(data, len) != len) return false;
    written += len;
    if (progress) progress(written, total);
    return true;
  }, status);
  file.close();

  if (notEpub) {
    sd.remove(path.c_str());
    return AbsResult::NOT_EPUB;
  }
  if (r != AbsResult::OK || written == 0) {
    sd.remove(path.c_str());
    return r == AbsResult::OK ? AbsResult::SERVER_ERROR : r;
  }
  return AbsResult::OK;
}

AbsResult parseFileJson(const std::string& path, JsonDocument& filter, JsonDocument& doc) {
  HalFile file;
  if (!Storage.openFileForRead("ABS", path.c_str(), file)) return AbsResult::SD_ERROR;
  FileReader reader{file};
  const DeserializationError err = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
  file.close();
  Storage.remove(path.c_str());
  if (err) {
    LOG_ERR("ABS", "JSON error: %s", err.c_str());
    return AbsResult::PARSE_ERROR;
  }
  return AbsResult::OK;
}

std::string apiUrl(const AbsConfig& cfg, const std::string& path) { return cfg.server + path; }

AbsResult fetchJsonFile(const AbsConfig& cfg, const std::string& path, JsonDocument& filter, JsonDocument& doc) {
  const AbsResult r = httpToFile(apiUrl(cfg, path), cfg.token, TMP_JSON, false, nullptr);
  if (r != AbsResult::OK) return r;
  return parseFileJson(TMP_JSON, filter, doc);
}

AbsResult tryLogin(const std::string& base, const AbsConfig& cfg, std::string& tokenOut) {
  JsonDocument payload;
  payload["username"] = cfg.user;
  payload["password"] = cfg.pass;
  std::string body;
  serializeJson(payload, body);

  std::string response;
  const AbsResult r = httpToString("POST", base + "/login", "", body, response);
  if (r != AbsResult::OK) return r;

  JsonDocument filter;
  AbsParse::loginFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, response, DeserializationOption::Filter(filter))) return AbsResult::PARSE_ERROR;
  const char* tok = doc["user"]["token"] | "";
  if (!*tok) tok = doc["user"]["accessToken"] | "";
  if (!*tok) return AbsResult::AUTH_ERROR;
  tokenOut = tok;
  return AbsResult::OK;
}

}

namespace AudiobookshelfPlugin {

void loadConfig(AbsConfig& cfg) {
  cfg.server = SETTINGS.absServer;
  cfg.user = SETTINGS.absUser;
  cfg.pass = SETTINGS.absPass;
  cfg.token = SETTINGS.absToken;
  cfg.library = SETTINGS.absLibrary[0] ? SETTINGS.absLibrary : "Books";
  cfg.savePass = SETTINGS.absSavePass != 0;
}

void saveConfig(const AbsConfig& cfg) {
  setField(SETTINGS.absServer, sizeof(SETTINGS.absServer), cfg.server);
  setField(SETTINGS.absUser, sizeof(SETTINGS.absUser), cfg.user);
  setField(SETTINGS.absPass, sizeof(SETTINGS.absPass), cfg.pass);
  setField(SETTINGS.absToken, sizeof(SETTINGS.absToken), cfg.token);
  SETTINGS.absSavePass = cfg.savePass ? 1 : 0;
  SETTINGS.saveToFile();
}

AbsResult login(AbsConfig& cfg) {
  cfg.server = AbsParse::normalizeServer(cfg.server);
  if (cfg.server.empty() || cfg.user.empty() || cfg.pass.empty()) return AbsResult::AUTH_ERROR;

  std::string token;
  AbsResult r = tryLogin(cfg.server, cfg, token);
  if (r == AbsResult::NOT_FOUND && !AbsParse::hasPath(cfg.server)) {
    const std::string alt = cfg.server + "/audiobookshelf";
    r = tryLogin(alt, cfg, token);
    if (r == AbsResult::OK) cfg.server = alt;
  }
  if (r != AbsResult::OK) return r;
  cfg.token = token;
  return AbsResult::OK;
}

AbsResult authorize(AbsConfig& cfg) {
  cfg.server = AbsParse::normalizeServer(cfg.server);
  if (cfg.server.empty() || cfg.token.empty()) return AbsResult::AUTH_ERROR;

  JsonDocument filter;
  AbsParse::librariesFilter(filter);
  JsonDocument doc;
  AbsResult r = fetchJsonFile(cfg, "/api/libraries", filter, doc);
  if (r != AbsResult::OK) return r;

  std::vector<AbsLibrary> libs;
  AbsParse::parseLibraries(doc, libs);
  g_libraryId = AbsParse::pickLibraryId(libs, cfg.library);
  return g_libraryId.empty() ? AbsResult::NO_LIBRARY : AbsResult::OK;
}

AbsResult loadShelves(const AbsConfig& cfg, std::vector<AbsShelf>& out) {
  if (g_libraryId.empty()) return AbsResult::NO_LIBRARY;
  JsonDocument filter;
  AbsParse::shelvesFilter(filter);
  JsonDocument doc;
  const AbsResult r = fetchJsonFile(
      cfg, "/api/libraries/" + g_libraryId + "/personalized?limit=" + std::to_string(SHELF_LIMIT), filter, doc);
  if (r != AbsResult::OK) return r;
  AbsParse::parseShelves(doc, out);
  return AbsResult::OK;
}

AbsResult search(const AbsConfig& cfg, const std::string& query, std::vector<AbsItem>& out) {
  if (g_libraryId.empty()) return AbsResult::NO_LIBRARY;
  JsonDocument filter;
  AbsParse::searchFilter(filter);
  JsonDocument doc;
  const AbsResult r = fetchJsonFile(
      cfg, "/api/libraries/" + g_libraryId + "/search?limit=" + std::to_string(SHELF_LIMIT * 2) + "&q=" +
               AbsParse::urlEncode(query),
      filter, doc);
  if (r != AbsResult::OK) return r;
  AbsParse::parseSearch(doc, out);
  return AbsResult::OK;
}

AbsResult downloadBook(const AbsConfig& cfg, const std::string& itemId, const std::string& destPath,
                       const AbsProgress& progress) {
  return httpToFile(apiUrl(cfg, "/api/items/" + itemId + "/ebook"), cfg.token, destPath, true, progress);
}

namespace {
constexpr int SCAN_MAX_DEPTH = 6;
constexpr int SCAN_MAX_ENTRIES = 6000;

bool scanForBook(const std::string& dirPath, const AbsItem& item, int depth, int& budget, std::string& found) {
  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }
  dir.rewindDirectory();
  char name[500];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (--budget <= 0) {
      entry.close();
      break;
    }
    entry.getName(name, sizeof(name));
    const std::string_view sv{name};
    if (sv.empty() || sv[0] == '.') {
      entry.close();
      continue;
    }
    std::string full = dirPath;
    if (full.back() != '/') full += '/';
    full += name;
    const bool isDir = entry.isDirectory();
    entry.close();
    if (isDir) {
      if (depth < SCAN_MAX_DEPTH && scanForBook(full, item, depth + 1, budget, found)) {
        dir.close();
        return true;
      }
    } else if (FsHelpers::hasEpubExtension(sv) && AbsParse::matchesExisting(full, item.title, item.author)) {
      found = full;
      dir.close();
      return true;
    }
  }
  dir.close();
  return false;
}
}  // namespace

std::string findExistingBook(const AbsItem& item) {
  std::string found;
  int budget = SCAN_MAX_ENTRIES;
  scanForBook("/", item, 0, budget, found);
  return found;
}

std::string bookPath(const AbsItem& item) { return "/" + AbsParse::sanitizeFilename(item.title, item.author, item.id); }

const char* message(AbsResult r) {
  switch (r) {
    case AbsResult::OK:
      return "OK";
    case AbsResult::NO_WIFI:
      return "No WiFi";
    case AbsResult::LOW_MEMORY:
      return "Low memory, exit and retry";
    case AbsResult::AUTH_ERROR:
      return "Login failed";
    case AbsResult::NOT_FOUND:
      return "Not found (check server URL)";
    case AbsResult::NO_LIBRARY:
      return "No ebook library found";
    case AbsResult::SERVER_ERROR:
      return "Server error";
    case AbsResult::NETWORK_ERROR:
      return "Cannot reach server";
    case AbsResult::PARSE_ERROR:
      return "Bad server response";
    case AbsResult::SD_ERROR:
      return "SD card error";
    case AbsResult::NOT_EPUB:
      return "No ebook file for this item";
  }
  return "Unknown error";
}

}
