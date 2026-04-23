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
    int         pct;   // overall book progress percentage (1-100)
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

std::string getIsbnFromEpub(Epub& epub, const std::string& path) {
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

int epubProgressPercent(const std::string& cachePath, int& outPage, Epub& epub) {
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

void collectBooks(std::vector<InProgressBook>& inProgress, std::vector<InProgressBook>& completed) {
    std::vector<std::string> allBooks;
    scanBooks("/", allBooks);

    for (const auto& path : allBooks) {
        std::string cachePath = buildCachePath(path);

        // Check progress.bin exists before loading the EPUB
        FsFile f;
        if (!Storage.openFileForRead("HCV", (cachePath + "/progress.bin").c_str(), f)) continue;
        f.close();

        int page = 0;
        Epub epub(path, CACHE_DIR);
        if (!epub.load(false, true) && !epub.load(true, true)) continue;

        int pct = epubProgressPercent(cachePath, page, epub);
        if (pct < 1) continue;

        std::string isbn = getIsbnFromEpub(epub, path);
        if (isbn.empty()) continue;

        if (pct >= 100)
            completed.push_back({isbn, page, pct});
        else
            inProgress.push_back({isbn, page, pct});
    }
}

// ---- HTTP helper -----------------------------------------------------------

String graphqlPost(const char* body, const std::string& token) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15);

    HTTPClient http;
    http.begin(client, HARDCOVER_API);
    http.setTimeout(15000);
    http.addHeader("Content-Type", "application/json");

    char authHeader[648];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", token.c_str());
    http.addHeader("Authorization", authHeader);

    int code = http.POST(body);
    if (code <= 0) {
        LOG_ERR("HCV", "HTTP error %d (connection failed — check WiFi/firewall)", code);
        http.end();
        return "";
    }
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

// Step 1: Look up book_id, page count, and user's user_book_id from ISBN-13.
bool lookupIds(const std::string& isbn, const std::string& token,
               int& outBookId, int& outUserBookId, int& outEditionPages) {
    // Query 1: edition lookup for book_id and pages
    char body[300];
    snprintf(body, sizeof(body),
        "{\"query\":\"{"
        "editions(where:{isbn_13:{_eq:\\\"%s\\\"}},limit:1){"
        "book_id pages"
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

    outBookId = editions[0]["book_id"] | 0;
    if (outBookId == 0) return false;
    outEditionPages = editions[0]["pages"] | 0;

    // Query 2: fetch current user's user_book_id for this book
    char body2[200];
    snprintf(body2, sizeof(body2),
        "{\"query\":\"{"
        "me{user_books(where:{book_id:{_eq:%d}},limit:1){id}}"
        "}\"}",
        outBookId);

    String resp2 = graphqlPost(body2, token);
    if (resp2.isEmpty()) return false;
    if (resp2.indexOf("\"errors\"") != -1) {
        LOG_ERR("HCV", "User book lookup error book_id=%d: %s", outBookId, resp2.c_str());
        return false;
    }

    JsonDocument doc2;
    if (deserializeJson(doc2, resp2) != DeserializationError::Ok) return false;

    JsonArray me = doc2["data"]["me"].as<JsonArray>();
    outUserBookId = 0;
    if (me.size() > 0) {
        JsonArray ubs = me[0]["user_books"].as<JsonArray>();
        if (ubs.size() > 0) outUserBookId = ubs[0]["id"] | 0;
    }

    LOG_DBG("HCV", "ISBN %s -> book_id=%d, pages=%d, user_book_id=%d",
            isbn.c_str(), outBookId, outEditionPages, outUserBookId);
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

// Returns cache path for storing the read session ID for a user_book
std::string readSessionCachePath(int userBookId) {
    char path[64];
    snprintf(path, sizeof(path), "%s/hardcover_%d.bin", CACHE_DIR, userBookId);
    return std::string(path);
}

int loadCachedReadId(int userBookId) {
    FsFile f;
    std::string path = readSessionCachePath(userBookId);
    if (!Storage.openFileForRead("HCV", path.c_str(), f)) return 0;
    uint8_t data[4] = {};
    bool ok = (f.read(data, 4) == 4);
    f.close();
    if (!ok) return 0;
    return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

void saveCachedReadId(int userBookId, int readId) {
    FsFile f;
    std::string path = readSessionCachePath(userBookId);
    if (!Storage.openFileForWrite("HCV", path.c_str(), f)) {
        LOG_ERR("HCV", "Could not save read session cache for user_book_id=%d", userBookId);
        return;
    }
    uint8_t data[4];
    data[0] = readId & 0xFF;
    data[1] = (readId >> 8) & 0xFF;
    data[2] = (readId >> 16) & 0xFF;
    data[3] = (readId >> 24) & 0xFF;
    f.write(data, 4);
    f.close();
}

bool updateProgress(int userBookId, int page, const std::string& token) {
    int cachedReadId = loadCachedReadId(userBookId);

    char body[512];
    if (cachedReadId != 0) {
        // Update existing cached session
        snprintf(body, sizeof(body),
            "{\"query\":\"mutation{"
            "update_user_book_read(id:%d,object:{progress_pages:%d}){"
            "id}}\"}",
            cachedReadId, page);
        String resp = graphqlPost(body, token);
        LOG_DBG("HCV", "updateProgress (cached read_id=%d): %s", cachedReadId, resp.c_str());
        if (!resp.isEmpty() && resp.indexOf("\"errors\"") == -1) return true;
        // Cache miss or stale — fall through to insert
        LOG_DBG("HCV", "Cached read_id stale, inserting new session");
    }

    // Insert a new read session and cache the ID
    snprintf(body, sizeof(body),
        "{\"query\":\"mutation{"
        "insert_user_book_read(user_book_id:%d,user_book_read:{progress_pages:%d}){"
        "id}}\"}",
        userBookId, page);
    String resp = graphqlPost(body, token);
    LOG_DBG("HCV", "insert_user_book_read: %s", resp.c_str());
    if (resp.isEmpty() || resp.indexOf("\"errors\"") != -1) {
        LOG_ERR("HCV", "updateProgress insert failed: %s", resp.c_str());
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;
    int newReadId = doc["data"]["insert_user_book_read"]["id"] | 0;
    if (newReadId != 0) saveCachedReadId(userBookId, newReadId);

    return true;
}

// New function to mark books as Read (status_id=3)
bool markAsRead(const std::string& isbn, const std::string& token) {
    int bookId = 0, userBookId = 0, editionPages = 0;
    if (!lookupIds(isbn, token, bookId, userBookId, editionPages)) return false;
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
    int bookId = 0, userBookId = 0, editionPages = 0;
    if (!lookupIds(book.isbn, token, bookId, userBookId, editionPages)) return false;

    // Ensure book is in library as Currently Reading
    int resolvedUserBookId = upsertUserBook(bookId, userBookId, token);
    if (resolvedUserBookId == 0) return false;

    // Compute absolute page from percentage and edition page count
    int absolutePage = 0;
    if (editionPages > 0)
        absolutePage = static_cast<int>(book.pct / 100.0f * editionPages + 0.5f);
    LOG_DBG("HCV", "Progress: %d%% -> page %d/%d", book.pct, absolutePage, editionPages);

    return updateProgress(resolvedUserBookId, absolutePage, token);
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

    std::vector<InProgressBook> books;
    std::vector<InProgressBook> completedBooks;
    collectBooks(books, completedBooks);

    if (books.empty() && completedBooks.empty()) {
        LOG_DBG("HCV", "No in-progress epub books with ISBN found.");
        return SyncResult::NO_BOOKS;
    }

    int userId = getUserId(token);
    if (userId == 0) return SyncResult::API_ERROR;

    bool allOk = true;

    for (const auto& b : books) {
        if (!syncBook(b, userId, token))
            allOk = false;
    }

    for (const auto& b : completedBooks) {
        if (!markAsRead(b.isbn, token))
            allOk = false;
    }
    
    return allOk ? SyncResult::OK : SyncResult::API_ERROR;
}

}  // namespace HardcoverPlugin
