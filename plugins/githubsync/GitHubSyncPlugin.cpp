#include "GitHubSyncPlugin.h"

#include <HTTPClient.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "CrossPointSettings.h"

namespace {

constexpr char GITHUB_API_HOST[] = "api.github.com";
constexpr char TREE_SHA_PATH[]   = "/.crosspoint/githubsync.sha";
constexpr size_t CHUNK_SIZE      = 4096;

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
    info.branch = "";
    return info;
}

std::string loadCachedTreeSha() {
    FsFile f;
    if (!Storage.openFileForRead("GHS", TREE_SHA_PATH, f)) return {};
    char buf[64] = {};
    f.read(buf, sizeof(buf) - 1);
    f.close();
    return std::string(buf);
}

void saveCachedTreeSha(const std::string& sha) {
    Storage.mkdir("/.crosspoint");
    FsFile f;
    if (!Storage.openFileForWrite("GHS", TREE_SHA_PATH, f)) return;
    f.write(reinterpret_cast<const uint8_t*>(sha.c_str()), sha.size());
    f.close();
}

bool downloadFile(const std::string& rawUrl, const std::string& pat,
                  const std::string& destPath) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, rawUrl.c_str());
    http.addHeader("User-Agent", "CrossPoint-GitHubSync/1.0");
    if (!pat.empty()) {
        std::string auth = "token " + pat;
        http.addHeader("Authorization", auth.c_str());
    }

    int code = http.GET();
    if (code != 200) {
        LOG_ERR("GHS", "HTTP %d for %s", code, rawUrl.c_str());
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
    uint8_t buf[CHUNK_SIZE];
    int total     = http.getSize();
    int remaining = total;

    while (http.connected() && (remaining > 0 || total == -1)) {
        size_t avail = stream->available();
        if (avail) {
            size_t toRead = std::min(avail, CHUNK_SIZE);
            size_t read   = stream->readBytes(buf, toRead);
            f.write(buf, read);
            if (total != -1) remaining -= read;
        }
        delay(1);
    }

    f.close();
    http.end();
    return true;
}

bool fetchTree(RepoInfo& info, const std::string& pat,
               std::string& outTreeSha,
               std::vector<std::pair<std::string, std::string>>& outFiles) {
    const char* branches[] = {"main", "master", nullptr};

    for (int i = 0; branches[i] != nullptr; i++) {
        if (!info.branch.empty() && info.branch != branches[i]) continue;

        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient http;
        char url[512];
        snprintf(url, sizeof(url),
                 "https://%s/repos/%s/%s/git/trees/%s?recursive=1",
                 GITHUB_API_HOST, info.owner.c_str(), info.repo.c_str(), branches[i]);

        http.begin(client, url);
        http.addHeader("User-Agent", "CrossPoint-GitHubSync/1.0");
        http.addHeader("Accept", "application/vnd.github+json");
        if (!pat.empty()) {
            std::string auth = "token " + pat;
            http.addHeader("Authorization", auth.c_str());
        }

        int code = http.GET();
        if (code != 200) {
            LOG_ERR("GHS", "Tree fetch HTTP %d for branch %s", code, branches[i]);
            http.end();
            continue;
        }

        String body = http.getString();
        http.end();

        if (body.isEmpty()) {
            LOG_ERR("GHS", "Empty response for branch %s", branches[i]);
            continue;
        }

        JsonDocument doc;
        if (deserializeJson(doc, body) != DeserializationError::Ok) {
            LOG_ERR("GHS", "JSON parse error");
            continue;
        }

        outTreeSha = std::string(doc["sha"] | "");
        info.branch = branches[i];

        JsonArray tree = doc["tree"].as<JsonArray>();
        for (JsonObject item : tree) {
            const char* type = item["type"] | "";
            const char* path = item["path"] | "";
            if (strcmp(type, "blob") != 0) continue;
            std::string p(path);

            bool wanted = false;
            if (p == "sleep.bmp") wanted = true;
            else if (p.rfind("sleep/", 0) == 0) wanted = true;
            else if (p.size() > 5 && p.substr(p.size() - 5) == ".epub") wanted = true;

            if (wanted)
                outFiles.push_back({p, std::string(item["sha"] | "")});
        }

        return true;
    }

    return false;
}

bool syncFiles(RepoInfo& info, const std::string& pat) {
    std::string treeSha;
    std::vector<std::pair<std::string, std::string>> files;
    if (!fetchTree(info, pat, treeSha, files)) return false;

    if (!treeSha.empty() && loadCachedTreeSha() == treeSha) {
        LOG_DBG("GHS", "Repo unchanged (tree SHA match), nothing to do.");
        return true;
    }

    for (const auto& [path, sha] : files) {
        std::string rawUrl =
            "https://raw.githubusercontent.com/" + info.owner + "/" +
            info.repo + "/" + info.branch + "/" + path;

        std::string dest = "/" + path;
        std::string dir  = dest.substr(0, dest.rfind('/'));
        if (!dir.empty() && dir != "/") Storage.mkdir(dir.c_str());

        LOG_DBG("GHS", "Downloading %s", path.c_str());
        if (!downloadFile(rawUrl, pat, dest)) {
            LOG_ERR("GHS", "Failed: %s", path.c_str());
            return false;
        }
        LOG_DBG("GHS", "OK: %s", path.c_str());
    }

    saveCachedTreeSha(treeSha);
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

    if (!syncFiles(info, pat)) return SyncResult::GIT_ERROR;

    return SyncResult::OK;
}

}
