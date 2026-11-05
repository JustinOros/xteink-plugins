#include "HardcoverPlugin.h"

#include <ArduinoJson.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <SecureHttpClient.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstring>
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
    int         pct;
    std::string title;
    std::string author;
};

// ---- ISBN extraction -------------------------------------------------------

std::string extractIsbnFromOpf(const uint8_t* data, size_t len) {
    const char* p   = reinterpret_cast<const char*>(data);
    const char* end = p + len;
    const char* pos = p;
    std::string asin;

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

        if (digits.size() == 13) return digits;
        if (digits.size() == 10) return "isbn10:" + digits;

        if (asin.empty() && value.size() == 10 && value[0] == 'B') {
            bool alnum = true;
            for (char c : value) {
                if (!isalnum(static_cast<unsigned char>(c))) { alnum = false; break; }
            }
            if (alnum) asin = "asin:" + value;
        }
    }
    return asin;
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
    HalFile f;
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
        HalFile f;
        if (!Storage.openFileForRead("HCV", (cachePath + "/progress.bin").c_str(), f)) continue;
        f.close();

        int page = 0;
        Epub epub(path, CACHE_DIR);
        if (!epub.load(false, true) && !epub.load(true, true)) continue;

        int pct = epubProgressPercent(cachePath, page, epub);
        if (pct < 1) {
            LOG_INF("HCV", "Skip %s: progress under 1%%", path.c_str());
            continue;
        }

        std::string isbn = getIsbnFromEpub(epub, path);
        if (isbn.empty() && epub.getTitle().empty()) {
            LOG_INF("HCV", "Skip %s: no ISBN, ASIN or title in book", path.c_str());
            continue;
        }

        if (pct >= 100)
            completed.push_back({isbn, page, pct, epub.getTitle(), epub.getAuthor()});
        else
            inProgress.push_back({isbn, page, pct, epub.getTitle(), epub.getAuthor()});
    }
}

// ---- HTTP helper -----------------------------------------------------------

String graphqlPost(const char* body, const std::string& token) {
    if (ESP.getFreeHeap() < 45000) {
        LOG_ERR("HCV", "Not enough free heap for TLS: %u", static_cast<unsigned>(ESP.getFreeHeap()));
        return "";
    }

    freeink::SecureHttpClient http;
    http.setInsecure();
    http.setTimeout(20000);
    http.setUserAgent("CrossPoint-X4");
    if (!http.begin(HARDCOVER_API)) {
        LOG_ERR("HCV", "Invalid API URL");
        return "";
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + token);

    std::string out;
    const int code = http.sendRequest(
        "POST", reinterpret_cast<const uint8_t*>(body), strlen(body),
        [&out](const uint8_t* data, size_t len) {
            if (out.size() + len > 32768) return false;
            out.append(reinterpret_cast<const char*>(data), len);
            return true;
        });
    if (code != 200) {
        LOG_ERR("HCV", "HTTP %d: %s", code, out.substr(0, 200).c_str());
        return "";
    }
    return String(out.c_str());
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
               int& outBookId, int& outUserBookId, int& outEditionPages, int& outEditionId) {
    // Query 1: edition lookup for book_id and pages
    const char* field = "isbn_13";
    std::string value = isbn;
    if (value.compare(0, 5, "asin:") == 0) {
        field = "asin";
        value = value.substr(5);
    } else if (value.compare(0, 7, "isbn10:") == 0) {
        field = "isbn_10";
        value = value.substr(7);
    }
    char body[320];
    snprintf(body, sizeof(body),
        "{\"query\":\"{"
        "editions(where:{%s:{_eq:\\\"%s\\\"}},limit:1){"
        "id book_id pages"
        "}}\"}",
        field, value.c_str());

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
    outEditionId = editions[0]["id"] | 0;

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
std::string plainWords(const std::string& s) {
    std::string out;
    bool lastSpace = true;
    for (char ch : s) {
        if (isalnum(static_cast<unsigned char>(ch))) {
            out += ch;
            lastSpace = false;
        } else if (!lastSpace) {
            out += ' ';
            lastSpace = true;
        }
        if (out.size() > 90) break;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string lastWord(const std::string& s) {
    std::string w;
    for (char ch : s) {
        if (isalnum(static_cast<unsigned char>(ch))) w += ch;
        else if (!w.empty() && ch == ' ') w.clear();
        else if (ch == ',') break;
    }
    return w;
}

bool findUserBook(int bookId, const std::string& token, int& outUserBookId) {
    char body2[200];
    snprintf(body2, sizeof(body2),
        "{\"query\":\"{me{user_books(where:{book_id:{_eq:%d}},limit:1){id}}}\"}", bookId);
    String resp2 = graphqlPost(body2, token);
    JsonDocument doc2;
    if (resp2.isEmpty() || resp2.indexOf("\"errors\"") != -1) return false;
    if (deserializeJson(doc2, resp2) != DeserializationError::Ok) return false;
    JsonArray me = doc2["data"]["me"].as<JsonArray>();
    outUserBookId = 0;
    if (me.size() > 0) {
        JsonArray ubs = me[0]["user_books"].as<JsonArray>();
        if (ubs.size() > 0) outUserBookId = ubs[0]["id"] | 0;
    }
    return true;
}

bool lookupByTitle(const std::string& title, const std::string& author, const std::string& token,
                   int& outBookId, int& outUserBookId, int& outEditionPages, int& outEditionId) {
    if (title.empty()) return false;
    std::string q = plainWords(title);
    const std::string authorWords = plainWords(author);
    if (!authorWords.empty()) q += " " + authorWords;
    const std::string authorKey = lastWord(author);

    char body[400];
    snprintf(body, sizeof(body),
        "{\"query\":\"{search(query:\\\"%s\\\",query_type:\\\"Book\\\",per_page:1,page:1){results}}\"}",
        q.c_str());
    String resp = graphqlPost(body, token);
    if (resp.isEmpty() || resp.indexOf("\"errors\"") != -1) {
        LOG_ERR("HCV", "Search error: %s", resp.c_str());
        return false;
    }
    std::string s(resp.c_str());
    LOG_INF("HCV", "Search response %u bytes: %.160s", static_cast<unsigned>(s.size()), s.c_str());
    for (size_t p = s.find("\\\""); p != std::string::npos; p = s.find("\\\"", p)) s.erase(p, 1);

    const size_t docPos = s.find("\"document\"");
    if (docPos == std::string::npos) {
        LOG_INF("HCV", "No search match for '%s'", q.c_str());
        return false;
    }
    int bookId = 0;
    {
        size_t idPos = s.find("\"id\":\"", docPos);
        if (idPos != std::string::npos) bookId = atoi(s.c_str() + idPos + 6);
    }
    if (bookId == 0) {
        LOG_ERR("HCV", "Search result had no id");
        return false;
    }

    std::string names;
    {
        size_t aPos = s.find("\"author_names\":[", docPos);
        if (aPos != std::string::npos) {
            size_t aEnd = s.find(']', aPos);
            if (aEnd != std::string::npos) names = s.substr(aPos, aEnd - aPos);
        }
    }
    auto squash = [](const std::string& in) {
        std::string out;
        for (char ch : in) {
            if (isalnum(static_cast<unsigned char>(ch))) out += static_cast<char>(tolower(static_cast<unsigned char>(ch)));
        }
        return out;
    };
    if (authorKey.size() >= 3) {
        if (names.empty()) {
            LOG_INF("HCV", "Search result has no authors, skipping");
            return false;
        }
        std::string lowNames = names, lowKey = authorKey;
        for (auto& ch : lowNames) ch = tolower(static_cast<unsigned char>(ch));
        for (auto& ch : lowKey) ch = tolower(static_cast<unsigned char>(ch));
        if (lowNames.find(lowKey) == std::string::npos) {
            LOG_INF("HCV", "Search result author mismatch for '%s': %s", q.c_str(), names.c_str());
            return false;
        }
    }

    char body2[200];
    snprintf(body2, sizeof(body2), "{\"query\":\"{books(where:{id:{_eq:%d}}){id title pages}}\"}", bookId);
    String resp2 = graphqlPost(body2, token);
    outEditionPages = 0;
    outEditionId = 0;
    std::string hitTitle;
    JsonDocument doc2;
    if (!resp2.isEmpty() && resp2.indexOf("\"errors\"") == -1 &&
        deserializeJson(doc2, resp2) == DeserializationError::Ok) {
        JsonArray books = doc2["data"]["books"].as<JsonArray>();
        if (books.size() > 0) {
            outEditionPages = books[0]["pages"] | 0;
            hitTitle = books[0]["title"] | "";
        }
    }
    {
        const std::string ta = squash(title), tb = squash(hitTitle);
        if (ta.empty() || tb.empty() || (ta.find(tb) == std::string::npos && tb.find(ta) == std::string::npos)) {
            LOG_INF("HCV", "Search result title mismatch: '%s' vs '%s' (book_id=%d)", title.c_str(), hitTitle.c_str(), bookId);
            return false;
        }
    }
    if (outEditionPages <= 0) {
        size_t pPos = s.find("\"pages\":", docPos);
        if (pPos != std::string::npos) outEditionPages = atoi(s.c_str() + pPos + 8);
    }
    if (outEditionPages <= 0) {
        char body3[240];
        snprintf(body3, sizeof(body3),
            "{\"query\":\"{editions(where:{book_id:{_eq:%d},pages:{_gt:0}},order_by:{users_count:desc},limit:1){id pages}}\"}", bookId);
        String resp3 = graphqlPost(body3, token);
        JsonDocument doc3;
        if (!resp3.isEmpty() && resp3.indexOf("\"errors\"") == -1 &&
            deserializeJson(doc3, resp3) == DeserializationError::Ok) {
            JsonArray eds = doc3["data"]["editions"].as<JsonArray>();
            if (eds.size() > 0) {
                outEditionPages = eds[0]["pages"] | 0;
                outEditionId = eds[0]["id"] | 0;
            }
        }
    }
    outBookId = bookId;
    if (!findUserBook(bookId, token, outUserBookId)) return false;
    LOG_INF("HCV", "Search match '%s' -> book_id=%d pages=%d user_book_id=%d",
            q.c_str(), bookId, outEditionPages, outUserBookId);
    return true;
}

bool lookupBook(const InProgressBook& book, const std::string& token,
                int& outBookId, int& outUserBookId, int& outEditionPages, int& outEditionId) {
    if (!book.isbn.empty() &&
        lookupIds(book.isbn, token, outBookId, outUserBookId, outEditionPages, outEditionId))
        return true;
    return lookupByTitle(book.title, book.author, token, outBookId, outUserBookId, outEditionPages, outEditionId);
}

// Step 2: Upsert user_book status to Currently Reading (status_id=2).
// Returns the user_book id (new or existing).
int upsertUserBook(int bookId, int userBookId, int editionId, const std::string& token) {
    char body[300];
    char editionField[40] = "";
    if (editionId != 0 && userBookId == 0) snprintf(editionField, sizeof(editionField), ",edition_id:%d", editionId);
    // insert_user_book acts as upsert via its error field
    snprintf(body, sizeof(body),
        "{\"query\":\"mutation{"
        "insert_user_book(object:{book_id:%d,status_id:2%s}){"
        "user_book{id} error"
        "}}\"}",
        bookId, editionField);

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

void saveCachedReadId(int userBookId, int readId) {
    HalFile f;
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

bool mutationOk(const String& resp, const char* field, int& outId) {
    outId = 0;
    if (resp.isEmpty() || resp.indexOf("\"errors\"") != -1) return false;
    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;
    JsonVariant node = doc["data"][field];
    const char* err = node["error"] | "";
    if (err[0] != '\0') {
        LOG_ERR("HCV", "%s error: %s", field, err);
        return false;
    }
    outId = node["id"] | 0;
    return outId != 0;
}

int unfinishedReadIds(int userBookId, const std::string& token, int* ids, int maxIds) {
    char body[300];
    snprintf(body, sizeof(body),
        "{\"query\":\"{"
        "user_book_reads(where:{user_book_id:{_eq:%d},finished_at:{_is_null:true}},order_by:{id:desc},limit:%d){id progress_pages}"
        "}\"}",
        userBookId, maxIds);
    String resp = graphqlPost(body, token);
    JsonDocument doc;
    if (resp.isEmpty() || resp.indexOf("\"errors\"") != -1) return 0;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return 0;
    JsonArray reads = doc["data"]["user_book_reads"].as<JsonArray>();
    int n = 0;
    for (JsonObject r : reads) {
        if (n >= maxIds) break;
        ids[n++] = r["id"] | 0;
        LOG_INF("HCV", "Open read %d progress_pages=%d", r["id"] | 0, r["progress_pages"] | -1);
    }
    return n;
}

bool updateProgress(int userBookId, int page, int editionId, const std::string& token) {
    char body[512];
    char editionField[40] = "";
    if (editionId != 0) snprintf(editionField, sizeof(editionField), ",edition_id:%d", editionId);

    int ids[8] = {};
    const int count = unfinishedReadIds(userBookId, token, ids, 8);
    int okCount = 0;
    for (int i = 0; i < count; i++) {
        snprintf(body, sizeof(body),
            "{\"query\":\"mutation{"
            "update_user_book_read(id:%d,object:{progress_pages:%d%s}){"
            "id error}}\"}",
            ids[i], page, editionField);
        String resp = graphqlPost(body, token);
        LOG_INF("HCV", "update_user_book_read(%d): %s", ids[i], resp.c_str());
        int id = 0;
        if (mutationOk(resp, "update_user_book_read", id)) okCount++;
    }
    if (okCount > 0) return true;

    snprintf(body, sizeof(body),
        "{\"query\":\"mutation{"
        "insert_user_book_read(user_book_id:%d,user_book_read:{progress_pages:%d%s}){"
        "id error}}\"}",
        userBookId, page, editionField);
    String resp = graphqlPost(body, token);
    LOG_INF("HCV", "insert_user_book_read: %s", resp.c_str());
    int newReadId = 0;
    if (!mutationOk(resp, "insert_user_book_read", newReadId)) {
        LOG_ERR("HCV", "updateProgress insert failed: %s", resp.c_str());
        return false;
    }
    saveCachedReadId(userBookId, newReadId);
    return true;
}

// New function to mark books as Read (status_id=3)
bool markAsRead(const InProgressBook& book, const std::string& token) {
    int bookId = 0, userBookId = 0, editionPages = 0, editionId = 0;
    if (!lookupBook(book, token, bookId, userBookId, editionPages, editionId)) return false;
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

bool userBookEdition(int userBookId, const std::string& token, int& editionId, int& pages) {
    char body[300];
    snprintf(body, sizeof(body),
        "{\"query\":\"{"
        "user_books(where:{id:{_eq:%d}}){edition{id pages}}"
        "}\"}",
        userBookId);
    String resp = graphqlPost(body, token);
    JsonDocument doc;
    if (resp.isEmpty() || resp.indexOf("\"errors\"") != -1) return false;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;
    JsonArray rows = doc["data"]["user_books"].as<JsonArray>();
    if (rows.size() == 0) return false;
    JsonVariant ed = rows[0]["edition"];
    const int id = ed["id"] | 0;
    const int pg = ed["pages"] | 0;
    if (id == 0 || pg <= 0) return false;
    editionId = id;
    pages = pg;
    return true;
}

bool syncBook(const InProgressBook& book, int userId, const std::string& token) {
    int bookId = 0, userBookId = 0, editionPages = 0, editionId = 0;
    if (!lookupBook(book, token, bookId, userBookId, editionPages, editionId)) return false;

    // Ensure book is in library as Currently Reading
    int resolvedUserBookId = upsertUserBook(bookId, userBookId, editionId, token);
    if (resolvedUserBookId == 0) return false;

    userBookEdition(resolvedUserBookId, token, editionId, editionPages);
    int absolutePage = 0;
    if (editionPages > 0)
        absolutePage = static_cast<int>(book.pct / 100.0f * editionPages + 0.5f);
    LOG_DBG("HCV", "Progress: %d%% -> page %d/%d", book.pct, absolutePage, editionPages);
    if (editionPages <= 0 || absolutePage <= 0) {
        LOG_ERR("HCV", "No page count known for '%s', cannot convert %d%% to a page", book.title.c_str(), book.pct);
        return false;
    }

    return updateProgress(resolvedUserBookId, absolutePage, editionId, token);
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
        if (!markAsRead(b, token))
            allOk = false;
    }
    
    return allOk ? SyncResult::OK : SyncResult::API_ERROR;
}

}  // namespace HardcoverPlugin
