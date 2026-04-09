#include "HardcoverPlugin.h"

#include <ArduinoJson.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <HTTPClient.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <string>
#include <vector>

#include "CrossPointSettings.h"

// ---------------------------------------------------------------------------
// Internal types & helpers
// ---------------------------------------------------------------------------

namespace {

constexpr char HARDCOVER_API[] = "https://api.hardcover.app/v1/graphql";
constexpr char CACHE_DIR[]     = "/.crosspoint";

struct InProgressBook {
    std::string isbn;
    int         page;
};

// ---- ISBN extraction -------------------------------------------------------

std::string extractIsbnFromOpf(const uint8_t* data, size_t len) {
    const char* p   = reinterpret_cast<const char*>(data);
    const char* end = p + len;
    const char* pos = p;

    while (pos < end) {
        const char* tagStart = static_cast<const char*>(
            memmem(pos, end - pos, "<dc:identifier", 14));
        if (!tagStart) break;

        const char* tagEnd = static_cast<const char*>(
            memchr(tagStart, '>', end - tagStart));
        if (!tagEnd) break;

        if (*(tagEnd - 1) == '/') { pos = tagEnd + 1; continue; }

        const char* valueStart = tagEnd + 1;
        const char* closeTag   = static_cast<const char*>(
            memmem(valueStart, end - valueStart, "</dc:identifier>", 16));
        if (!closeTag) break;

        std::string value(valueStart, closeTag - valueStart);
        pos = closeTag + 16;

        for (const char* prefix : {"urn:isbn:", "isbn:", "ISBN:", "URN:ISBN:"}) {
            size_t plen = strlen(prefix);
            if (value.size() >= plen && value.compare(0, plen, prefix) == 0) {
                value = value.substr(plen);
                break;
            }
        }

        std::string digits;
        for (char c : value) {
            if (isdigit(static_cast<unsigned char>(c)) || c == 'X' || c == 'x')
                digits += c;
        }

        if (digits.size() == 13 || digits.size() == 10)
            return digits;
    }
    return {};
}

std::string getIsbnFromEpub(const std::string& path) {
    Epub epub(path, CACHE_DIR);
    if (!epub.load(false, true) && !epub.load(true, true)) {
        LOG_DBG("HCV", "Could not load epub: %s", path.c_str());
        return {};
    }

    const char* opfCandidates[] = {
        "OEBPS/content.opf", "OPS/content.opf", "content.opf",
        "EPUB/content.opf",  nullptr
    };
    for (int i = 0; opfCandidates[i]; i++) {
        size_t size = 0;
        uint8_t* data = epub.readItemContentsToBytes(opfCandidates[i], &size, false);
        if (!data || size == 0) { if (data) free(data); continue; }
        std::string isbn = extractIsbnFromOpf(data, size);
        free(data);
        if (!isbn.empty()) return isbn;
    }

    size_t cSize = 0;
    uint8_t* cData = epub.readItemContentsToBytes("META-INF/container.xml", &cSize, true);
    if (cData && cSize > 0) {
        std::string container(reinterpret_cast<char*>(cData), cSize);
        free(cData);
        const char* marker = "full-path=\"";
        auto mpos = container.find(marker);
        if (mpos != std::string::npos) {
            mpos += strlen(marker);
            auto mend = container.find('"', mpos);
            if (mend != std::string::npos) {
                std::string opfPath = container.substr(mpos, mend - mpos);
                size_t oSize = 0;
                uint8_t* oData = epub.readItemContentsToBytes(opfPath.c_str(), &oSize, false);
                if (oData && oSize > 0) {
                    std::string isbn = extractIsbnFromOpf(oData, oSize);
                    free(oData);
                    if (!isbn.empty()) return isbn;
                } else { if (oData) free(oData); }
            }
        }
    } else { if (cData) free(cData); }

    LOG_DBG("HCV", "No ISBN found in %s", path.c_str());
    return {};
}

// ---- Progress reading -------------------------------------------------------

int epubProgressPercent(const std::string& path, const std::string& cachePath, int& outPage) {
    FsFile f;
    if (!Storage.openFileForRead("HCV", (cachePath + "/progress.bin").c_str(), f))
        return -1;
    uint8_t data[6] = {};
    bool ok = (f.read(data, 6) == 6);
    f.close();
    if (!ok) return -1;

    int spineIndex  = data[0] | (data[1] << 8);
    int currentPage = data[2] | (data[3] << 8);
    int pageCount   = data[4] | (data[5] << 8);
    if (pageCount <= 0) return -1;

    outPage = currentPage;

    float spineProgress = static_cast<float>(currentPage) / static_cast<float>(pageCount);

    Epub epub(path, CACHE_DIR);
    if (!epub.load(false, true) && !epub.load(true, true)) return -1;

    float pct = epub.calculateProgress(spineIndex, spineProgress) * 100.0f;
    return static_cast<int>(pct + 0.5f);
}

// ---- Filesystem scan -------------------------------------------------------

void scanBooks(const std::string& rootPath, std::vector<std::string>& out) {
    auto dir = Storage.open(rootPath.c_str());
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
    dir.rewindDirectory();
    char name[500];
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
        entry.getName(name, sizeof(name));
        std::string_view sv{name};
        if (sv.empty() || sv[0] == '.') { entry.close(); continue; }
        std::string fullPath = rootPath;
        if (fullPath.back() != '/') fullPath += '/';
        fullPath += name;
        if (entry.isDirectory()) {
            entry.close();
            scanBooks(fullPath, out);
        } else if (FsHelpers::hasEpubExtension(sv)) {
            out.push_back(fullPath);
        }
        entry.close();
    }
    dir.close();
}

std::string buildCachePath(const std::string& filePath) {
    return std::string(CACHE_DIR) + "/epub_" +
           std::to_string(std::hash<std::string>{}(filePath));
}

std::vector<InProgressBook> collectInProgressBooks() {
    std::vector<std::string> allBooks;
    scanBooks("/", allBooks);

    std::vector<InProgressBook> result;
    for (const auto& path : allBooks) {
        std::string cachePath = buildCachePath(path);
        int page = 0;
        int pct  = epubProgressPercent(path, cachePath, page);
        if (pct < 1) continue;  // Skip 0% books, include 1-100%+

        std::string isbn = getIsbnFromEpub(path);
        if (isbn.empty()) continue;

        result.push_back({isbn, page});
    }
    return result;
}

// New function to collect completed books (100%+)
std::vector<InProgressBook> collectCompletedBooks() {
    std::vector<std::string> allBooks;
    scanBooks("/", allBooks);

    std::vector<InProgressBook> result;
    for (const auto& path : allBooks) {
        std::string cachePath = buildCachePath(path);
        int page = 0;
        int pct  = epubProgressPercent(path, cachePath, page);
        if (pct < 100) continue;  // Only include 100%+ books

        std::string isbn = getIsbnFromEpub(path);
        if (isbn.empty()) continue;

        result.push_back({isbn, page});
    }
    return result;
}

// ---- HTTP helper -----------------------------------------------------------

String graphqlPost(const char* body, const std::string& token) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, HARDCOVER_API);
    http.addHeader("Content-Type", "application/json");

    char authHeader[648];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", token.c_str());
    http.addHeader("Authorization", authHeader);

    int code = http.POST(body);
    if (code != 200) {
        LOG_ERR("HCV", "HTTP %d", code);
        http.end();
        return "";
    }
    String resp = http.getString();
    http.end();
    return resp;
}

// ---- Hardcover API ---------------------------------------------------------

// Get the authenticated user's ID.
int getUserId(const std::string& token) {
    const char* body = "{\"query\":\"{ me { id } }\"}";
    String resp = graphqlPost(body, token);
    if (resp.isEmpty() || resp.indexOf("\"errors\"") != -1) {
        LOG_ERR("HCV", "getUserId failed: %s", resp.c_str());
        return 0;
    }
    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return 0;
    
    // me returns an array, so access the first element
    JsonArray meArray = doc["data"]["me"];
    if (meArray.size() == 0) return 0;
    
    int id = meArray[0]["id"] | 0;
    LOG_DBG("HCV", "user_id=%d", id);
    return id;
}

// Step 1: Look up book_id from ISBN-13 via editions table.
// Also returns existing user_book id (0 if none).
bool lookupIds(const std::string& isbn, const std::string& token,
               int& outBookId, int& outUserBookId) {
    char body[400];
    snprintf(body, sizeof(body),
        "{\"query\":\"{"
        "editions(where:{isbn_13:{_eq:\\\"%s\\\"}},limit:1){"
        "book_id "
        "book{id user_books(limit:1){id}}"
        "}}\"}",
        isbn.c_str());

    String resp = graphqlPost(body, token);
    if (resp.isEmpty()) return false;
    if (resp.indexOf("\"errors\"") != -1) {
        LOG_ERR("HCV", "Lookup error ISBN %s: %s", isbn.c_str(), resp.c_str());
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;

    JsonArray editions = doc["data"]["editions"].as<JsonArray>();
    if (editions.size() == 0) {
        LOG_DBG("HCV", "No edition for ISBN %s", isbn.c_str());
        return false;
    }

    outBookId = editions[0]["book"]["id"] | 0;
    if (outBookId == 0) return false;

    JsonArray ubs = editions[0]["book"]["user_books"].as<JsonArray>();
    outUserBookId = (ubs.size() > 0) ? (ubs[0]["id"] | 0) : 0;

    LOG_DBG("HCV", "ISBN %s -> book_id=%d", isbn.c_str(), outBookId);
    return true;
}

// Step 2: Upsert user_book status to Currently Reading (status_id=2).
// Returns the user_book id (new or existing).
int upsertUserBook(int bookId, int userBookId, const std::string& token) {
    char body[300];
    // insert_user_book acts as upsert via its error field
    snprintf(body, sizeof(body),
        "{\"query\":\"mutation{"
        "insert_user_book(object:{book_id:%d,status_id:2}){"
        "user_book{id} error"
        "}}\"}",
        bookId);

    String resp = graphqlPost(body, token);
    if (resp.isEmpty()) return 0;
    if (resp.indexOf("\"errors\"") != -1) {
        LOG_ERR("HCV", "insert_user_book error book_id=%d: %s", bookId, resp.c_str());
        return 0;
    }

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return 0;

    // If there was a conflict (book already in library), returns existing id
    int id = doc["data"]["insert_user_book"]["user_book"]["id"] | 0;
    if (id == 0 && userBookId != 0) id = userBookId;  // fallback to known id

    return id;
}

bool updateProgress(int userId, int bookId, int page, const std::string& token) {
    char body[512];
    snprintf(body, sizeof(body),
        "{\"query\":\"mutation{"
        "insert_user_book_read(user_book_id:%d,user_book_read:{progress_pages:%d}){"
        "id}}\"}",
        bookId, page);
    String resp = graphqlPost(body, token);
    LOG_DBG("HCV", "insert_user_book_read: %s", resp.c_str());
    if (!resp.isEmpty() && resp.indexOf("\"errors\"") == -1) return true;
    LOG_ERR("HCV", "updateProgress failed: %s", resp.c_str());
    return false;
}

// New function to mark books as Read (status_id=3)
bool markAsRead(const std::string& isbn, const std::string& token) {
    int bookId = 0, userBookId = 0;
    if (!lookupIds(isbn, token, bookId, userBookId)) return false;
    if (userBookId == 0) return false;

    char body[300];
    snprintf(body, sizeof(body),
        "{\"query\":\"mutation{"
        "update_user_book(id:%d,object:{status_id:3}){"
        "id}}\"}",
        userBookId);
    String resp = graphqlPost(body, token);
    LOG_DBG("HCV", "markAsRead response: %s", resp.c_str());
    return (!resp.isEmpty() && resp.indexOf("\"errors\"") == -1);
}

bool syncBook(const InProgressBook& book, int userId, const std::string& token) {
    int bookId = 0, userBookId = 0;
    if (!lookupIds(book.isbn, token, bookId, userBookId)) return false;

    // Ensure book is in library as Currently Reading
    int resolvedUserBookId = upsertUserBook(bookId, userBookId, token);
    if (resolvedUserBookId == 0) return false;

    return updateProgress(userId, resolvedUserBookId, book.page, token);
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace HardcoverPlugin {

SyncResult syncProgress() {
    if (WiFi.status() != WL_CONNECTED) {
        LOG_ERR("HCV", "Not connected to WiFi");
        return SyncResult::NO_WIFI;
    }

    if (SETTINGS.hardcoverApiToken[0] == '\0') {
        LOG_ERR("HCV", "No Hardcover API token configured");
        return SyncResult::NO_TOKEN;
    }
    const std::string token(SETTINGS.hardcoverApiToken);

    auto books = collectInProgressBooks();

    if (books.empty()) {
        LOG_DBG("HCV", "No in-progress epub books with ISBN found.");
        return SyncResult::NO_BOOKS;
    }

    int userId = getUserId(token);
    if (userId == 0) return SyncResult::API_ERROR;

    bool allOk = true;
    
    // Sync progress for all books with > 0% completion
    for (const auto& b : books) {
        if (!syncBook(b, userId, token))
            allOk = false;
    }
    
    // Mark 100%+ books as Read
    auto completedBooks = collectCompletedBooks();
    for (const auto& b : completedBooks) {
        if (!markAsRead(b.isbn, token))
            allOk = false;
    }
    
    return allOk ? SyncResult::OK : SyncResult::API_ERROR;
}

}  // namespace HardcoverPlugin

