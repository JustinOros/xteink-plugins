#include "GitHubSyncPlugin.h"

#include <HTTPClient.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>
#include <ArduinoJson.h>

#include <string>

#include "CrossPointSettings.h"

namespace {

constexpr char GH_API_BASE[]  = "https://api.github.com";
constexpr char GH_SHA_DIR[]   = "/.crosspoint/github_sha/";
constexpr char GH_SLEEP_BMP[] = "sleep.bmp";

struct RepoInfo {
    std::string owner;
    std::string repo;
    std::string branch;
};

RepoInfo parseRepoUrl(const std::string& url) {
    RepoInfo info;
    std::string u = url;

    for (const char* prefix : {"https://github.com/", "http://github.com/", "github.com/"}) {
        if (u.rfind(prefix, 0) == 0) {
            u = u.substr(strlen(prefix));
            break;
        }
    }

    if (!u.empty() && u.back() == '/') u.pop_back();
    if (u.size() > 4 && u.substr(u.size() - 4) == ".git")
        u = u.substr(0, u.size() - 4);

    auto slash = u.find('/');
    if (slash == std::string::npos) return info;
    info.owner  = u.substr(0, slash);
    info.repo   = u.substr(slash + 1);
    info.branch = "main";
    return info;
}

std::string shaFilePath(const std::string& filename) {
    std::string safe = filename;
    for (char& c : safe) if (c == '/') c = '_';
    return std::string(GH_SHA_DIR) + safe + ".sha";
}

std::string loadLocalSha(const std::string& filename) {
    FsFile f;
    if (!Storage.openFileForRead("GHS", shaFilePath(filename).c_str(), f)) return "";
    char buf[41] = {};
    f.read(buf, sizeof(buf) - 1);
    f.close();
    std::string sha(buf);
    while (!sha.empty() && (sha.back() == '\n' || sha.back() == '\r' || sha.back() == ' '))
        sha.pop_back();
    return sha;
}

void saveLocalSha(const std::string& filename, const std::string& sha) {
    Storage.mkdir(GH_SHA_DIR);
    FsFile f;
    if (!Storage.openFileForWrite("GHS", shaFilePath(filename).c_str(), f)) return;
    f.write(reinterpret_cast<const uint8_t*>(sha.c_str()), sha.size());
    f.close();
}

bool downloadFile(const std::string& downloadUrl, const std::string& pat,
                  const std::string& destPath, const std::string& name,
                  const std::string& sha) {
    HTTPClient http;
    LOG_INF("GHS", "Download URL: %s", downloadUrl.c_str());
    http.begin(downloadUrl.c_str());
    http.setTimeout(30000);
    http.addHeader("Authorization", ("token " + pat).c_str());
    http.addHeader("User-Agent", "CrossPoint-GitHubSync/1.0");

    int code = http.GET();
    LOG_INF("GHS", "Download '%s' HTTP %d", name.c_str(), code);
    if (code != 200) {
        http.end();
        return false;
    }

    FsFile f;
    if (!Storage.openFileForWrite("GHS", destPath.c_str(), f)) {
        LOG_ERR("GHS", "Cannot open %s for write", destPath.c_str());
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[512];
    int total = http.getSize();
    int remaining = total;
    unsigned long lastData = millis();

    while (http.connected() && (remaining > 0 || total == -1)) {
        size_t avail = stream->available();
        if (avail) {
            size_t toRead = avail < sizeof(buf) ? avail : sizeof(buf);
            size_t read   = stream->readBytes(buf, toRead);
            f.write(buf, read);
            if (remaining > 0) remaining -= (int)read;
            lastData = millis();
        } else {
            if (millis() - lastData > 30000) {
                LOG_ERR("GHS", "Download timed out: %s", name.c_str());
                f.close();
                http.end();
                return false;
            }
            delay(10);
        }
    }

    f.close();
    http.end();
    saveLocalSha(name, sha);
    return true;
}

bool syncContents(const RepoInfo& info, const std::string& pat) {
    std::string url = std::string(GH_API_BASE) + "/repos/" + info.owner + "/" +
                      info.repo + "/contents/?ref=" + info.branch;

    HTTPClient http;
    http.begin(url.c_str());
    http.setTimeout(15000);
    http.addHeader("Authorization", ("token " + pat).c_str());
    http.addHeader("Accept", "application/vnd.github.v3+json");
    http.addHeader("User-Agent", "CrossPoint-GitHubSync/1.0");

    int code = http.GET();
    LOG_INF("GHS", "Contents API HTTP %d", code);
    if (code != 200) {
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        LOG_ERR("GHS", "JSON parse error");
        return false;
    }

    JsonArray files = doc.as<JsonArray>();

    for (JsonObject file : files) {
        const char* type        = file["type"] | "";
        const char* name        = file["name"] | "";
        const char* sha         = file["sha"]  | "";
        const char* downloadUrl = file["download_url"] | "";

        if (strcmp(type, "file") != 0) continue;

        std::string n(name);
        bool isEpub  = n.size() > 5 && (n.substr(n.size() - 5) == ".epub" ||
                                         n.substr(n.size() - 5) == ".EPUB");
        bool isSleep = (n == GH_SLEEP_BMP);
        if (!isEpub && !isSleep) continue;
        if (!downloadUrl || !*downloadUrl) continue;

        std::string destPath = isSleep ? "/sleep.bmp" : "/" + n;
        std::string localSha = loadLocalSha(n);

        if (localSha == std::string(sha) && Storage.exists(destPath.c_str())) {
            LOG_DBG("GHS", "Up to date: %s", name);
            continue;
        }

        if (Storage.exists(destPath.c_str())) {
            LOG_DBG("GHS", "File exists, skipping: %s", name);
            continue;
        }

        LOG_INF("GHS", "Downloading: %s", name);
        if (!downloadFile(std::string(downloadUrl), pat, destPath, n, std::string(sha)))
            return false;
    }

    return true;
}

}

namespace GitHubSyncPlugin {

SyncResult sync() {
    if (WiFi.status() != WL_CONNECTED) {
        LOG_ERR("GHS", "No WiFi");
        return SyncResult::NO_WIFI;
    }

    const std::string url(SETTINGS.githubSyncUrl);
    if (url.empty()) {
        LOG_ERR("GHS", "No GitHub URL configured");
        return SyncResult::GIT_ERROR;
    }

    const std::string pat(SETTINGS.githubSyncPat);
    RepoInfo info = parseRepoUrl(url);

    if (info.owner.empty() || info.repo.empty()) {
        LOG_ERR("GHS", "Invalid GitHub URL: %s", url.c_str());
        return SyncResult::GIT_ERROR;
    }

    if (!syncContents(info, pat)) return SyncResult::GIT_ERROR;

    return SyncResult::OK;
}

}
