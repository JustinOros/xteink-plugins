#include "GitHubSync.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Logging.h>
#include "CrossPointSettings.h"
#include "SDCardManager.h"

#define GH_SHA_DIR    "/.crosspoint/github_sha/"
#define GH_BOOKS_DIR  "/"
#define GH_SLEEP_BMP  "sleep.bmp"
#define GH_SLEEP_PATH "/sleep.bmp"
#define GH_API_BASE   "https://api.github.com"

namespace {
void setField(char* field, size_t fieldSize, const std::string& value) {
    strncpy(field, value.c_str(), fieldSize - 1);
    field[fieldSize - 1] = '\0';
}
}  // namespace

// Config now lives in CrossPointSettings (persisted via SETTINGS.saveToFile()/
// loadFromFile(), same as every other plugin setting) instead of a private NVS
// namespace, so it's reachable through the standard settings list - which is
// what makes it show up in the web UI's Settings page alongside Dark Mode,
// Smaller Fonts, etc. On-device editing via GitHubSyncSettingsActivity is
// unaffected since it only goes through this struct-based API.
bool GitHubSync::loadConfig(GitHubSyncConfig &cfg) {
    cfg.username = SETTINGS.githubUsername;
    cfg.pat      = SETTINGS.githubPat;
    cfg.repo     = SETTINGS.githubRepo[0] ? SETTINGS.githubRepo : "xteink";
    cfg.branch   = SETTINGS.githubBranch[0] ? SETTINGS.githubBranch : "main";
    return !cfg.username.empty() && !cfg.pat.empty();
}

void GitHubSync::saveConfig(const GitHubSyncConfig &cfg) {
    setField(SETTINGS.githubUsername, sizeof(SETTINGS.githubUsername), cfg.username);
    setField(SETTINGS.githubPat,      sizeof(SETTINGS.githubPat),      cfg.pat);
    setField(SETTINGS.githubRepo,     sizeof(SETTINGS.githubRepo),     cfg.repo);
    setField(SETTINGS.githubBranch,   sizeof(SETTINGS.githubBranch),   cfg.branch);
    SETTINGS.saveToFile();
}

bool GitHubSync::isConfigured() {
    GitHubSyncConfig cfg;
    return loadConfig(cfg);
}

const char* GitHubSync::resultMessage(GitHubSyncResult r) {
    switch (r) {
        case GitHubSyncResult::OK:             return "Sync complete";
        case GitHubSyncResult::NOT_CONFIGURED: return "Not configured";
        case GitHubSyncResult::NO_WIFI:        return "No WiFi";
        case GitHubSyncResult::AUTH_ERROR:     return "Auth failed (check PAT)";
        case GitHubSyncResult::REPO_NOT_FOUND: return "Repo not found";
        case GitHubSyncResult::API_ERROR:      return "API error";
        case GitHubSyncResult::SD_ERROR:       return "SD card error";
        case GitHubSyncResult::PARSE_ERROR:    return "Bad API response";
        default:                               return "Unknown error";
    }
}

std::string GitHubSync::shaFilePath(const std::string &filename) {
    std::string safe = filename;
    for (char &c : safe) if (c == '/') c = '_';
    return std::string(GH_SHA_DIR) + safe + ".sha";
}

std::string GitHubSync::loadLocalSha(const std::string &filename) {
    std::string path = shaFilePath(filename);
    auto &sd = SDCardManager::getInstance();
    FsFile f;
    if (!sd.openFileForRead("SYNC", path, f)) return "";
    char buf[41] = {};
    f.read(buf, sizeof(buf) - 1);
    f.close();
    std::string sha(buf);
    while (!sha.empty() && (sha.back() == '\n' || sha.back() == '\r' || sha.back() == ' '))
        sha.pop_back();
    return sha;
}

void GitHubSync::saveLocalSha(const std::string &filename, const std::string &sha) {
    auto &sd = SDCardManager::getInstance();
    sd.mkdir(GH_SHA_DIR);
    std::string path = shaFilePath(filename);
    FsFile f;
    if (!sd.openFileForWrite("SYNC", path, f)) return;
    f.print(sha.c_str());
    f.close();
}

bool GitHubSync::fetchFileList(const GitHubSyncConfig &cfg, std::string &outJson, GitHubSyncResult &err) {
    std::string url = std::string(GH_API_BASE) + "/repos/" + cfg.username + "/" +
                      cfg.repo + "/contents/?ref=" + cfg.branch;

    HTTPClient http;
    http.begin(url.c_str());
    http.addHeader("Authorization", ("token " + cfg.pat).c_str());
    http.addHeader("Accept", "application/vnd.github.v3+json");
    http.addHeader("User-Agent", "CrossPoint-X4");

    int code = http.GET();
    LOG_INF("SYNC", "fetchFileList HTTP code: %d", code);
    if (code == 401 || code == 403) { http.end(); err = GitHubSyncResult::AUTH_ERROR;     return false; }
    if (code == 404)                { http.end(); err = GitHubSyncResult::REPO_NOT_FOUND; return false; }
    if (code != 200)                { http.end(); err = GitHubSyncResult::API_ERROR;      return false; }

    outJson = http.getString().c_str();
    http.end();
    return true;
}

bool GitHubSync::downloadFile(const GitHubSyncConfig &cfg, const std::string &downloadUrl, const std::string &name, const std::string &sha, GitHubSyncResult &err, GitHubSyncProgressCallback onProgress) {
    HTTPClient http;
    http.begin(downloadUrl.c_str());
    http.addHeader("Authorization", ("token " + cfg.pat).c_str());
    http.addHeader("User-Agent", "CrossPoint-X4");

    int code = http.GET();
    LOG_INF("SYNC", "downloadFile '%s' HTTP code: %d", name.c_str(), code);
    if (code == 401 || code == 403) { http.end(); err = GitHubSyncResult::AUTH_ERROR; return false; }
    if (code != 200)                { http.end(); err = GitHubSyncResult::API_ERROR;  return false; }

    std::string destPath = (name == GH_SLEEP_BMP) ? GH_SLEEP_PATH : std::string(GH_BOOKS_DIR) + name;
    LOG_INF("SYNC", "Writing to SD path: '%s'", destPath.c_str());

    auto &sd = SDCardManager::getInstance();
    LOG_INF("SYNC", "SDCardManager ready: %s", sd.ready() ? "YES" : "NO");

    if (onProgress) onProgress("Writing...");

    FsFile f;
    if (!sd.openFileForWrite("SYNC", destPath, f)) {
        http.end();
        err = GitHubSyncResult::SD_ERROR;
        return false;
    }
    LOG_INF("SYNC", "File opened for write OK");

    WiFiClient *stream = http.getStreamPtr();
    uint8_t buf[512];
    int total = http.getSize();
    int remaining = total;
    unsigned long lastData = millis();
    const unsigned long timeoutMs = 30000;

    while (http.connected() && (remaining > 0 || total == -1)) {
        size_t available = stream->available();
        if (available) {
            size_t read = stream->readBytes(buf, min(available, sizeof(buf)));
            f.write(buf, read);
            if (remaining > 0) remaining -= (int)read;
            lastData = millis();
        } else {
            if (millis() - lastData > timeoutMs) {
                LOG_INF("SYNC", "Download timed out after 30s");
                f.close();
                http.end();
                err = GitHubSyncResult::API_ERROR;
                return false;
            }
            delay(10);
        }
    }

    f.close();
    http.end();
    saveLocalSha(name, sha);
    if (onProgress) onProgress("Saved");
    return true;
}

GitHubSyncResult GitHubSync::sync(GitHubSyncProgressCallback onProgress) {
    GitHubSyncConfig cfg;
    if (!loadConfig(cfg)) return GitHubSyncResult::NOT_CONFIGURED;
    if (WiFi.status() != WL_CONNECTED) return GitHubSyncResult::NO_WIFI;

    if (onProgress) onProgress("Checking...");

    std::string jsonStr;
    GitHubSyncResult err = GitHubSyncResult::OK;
    if (!fetchFileList(cfg, jsonStr, err)) return err;

    JsonDocument doc;
    DeserializationError jsonErr = deserializeJson(doc, jsonStr);
    if (jsonErr) return GitHubSyncResult::PARSE_ERROR;

    JsonArray files = doc.as<JsonArray>();
    int downloaded = 0;
    for (JsonObject file : files) {
        std::string type        = file["type"].as<const char*>();
        std::string name        = file["name"].as<const char*>();
        std::string sha         = file["sha"].as<const char*>();
        std::string downloadUrl = file["download_url"].as<const char*>();

        if (type != "file") continue;

        bool isEpub  = name.size() > 5 && (name.substr(name.size()-5) == ".epub" || name.substr(name.size()-5) == ".EPUB");
        bool isSleep = (name == GH_SLEEP_BMP);

        if (!isEpub && !isSleep) continue;
        if (downloadUrl.empty()) continue;

        std::string localSha = loadLocalSha(name);
        std::string destPath = (name == GH_SLEEP_BMP) ? GH_SLEEP_PATH : std::string(GH_BOOKS_DIR) + name;
        bool fileExists = SDCardManager::getInstance().exists(destPath.c_str());

        if (localSha == sha && fileExists) {
            if (onProgress) onProgress("Up to date");
            continue;
        }

        if (fileExists && localSha.empty()) {
            LOG_INF("SYNC", "Adopting remote SHA for existing file '%s'", name.c_str());
            saveLocalSha(name, sha);
            if (onProgress) onProgress("Up to date");
            continue;
        }

        if (onProgress) onProgress("Downloading...");
        LOG_INF("SYNC", "Downloading '%s'", name.c_str());
        if (!downloadFile(cfg, downloadUrl, name, sha, err, onProgress)) return err;
        downloaded++;
    }

    if (onProgress) {
        if (downloaded == 0)
            onProgress("Up to date");
        else
            onProgress("Synchronized");
    }

    return GitHubSyncResult::OK;
}
