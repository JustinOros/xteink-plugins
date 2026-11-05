#pragma once

#include <ArduinoJson.h>

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

struct AbsItem {
  std::string id;
  std::string title;
  std::string author;
  bool isSeries = false;
  std::vector<AbsItem> books;
};

struct AbsShelf {
  std::string id;
  std::vector<AbsItem> items;
};

struct AbsLibrary {
  std::string id;
  std::string name;
  std::string mediaType;
};

namespace AbsParse {

inline void bookFilter(JsonObject f) {
  f["id"] = true;
  f["media"]["metadata"]["title"] = true;
  f["media"]["metadata"]["authorName"] = true;
  f["media"]["ebookFormat"] = true;
}

inline void shelvesFilter(JsonDocument& f) {
  f[0]["id"] = true;
  JsonObject e = f[0]["entities"][0].to<JsonObject>();
  bookFilter(e);
  e["name"] = true;
  bookFilter(e["books"][0].to<JsonObject>());
}

inline void searchFilter(JsonDocument& f) {
  bookFilter(f["book"][0]["libraryItem"].to<JsonObject>());
}

inline void librariesFilter(JsonDocument& f) {
  f["libraries"][0]["id"] = true;
  f["libraries"][0]["name"] = true;
  f["libraries"][0]["mediaType"] = true;
}

inline void loginFilter(JsonDocument& f) {
  f["user"]["token"] = true;
  f["user"]["accessToken"] = true;
}

inline bool parseBook(JsonVariantConst v, AbsItem& out) {
  const char* id = v["id"] | "";
  if (!*id) return false;
  JsonVariantConst media = v["media"];
  if (media["ebookFormat"].is<JsonVariantConst>()) {
    const char* fmt = media["ebookFormat"] | "";
    if (!*fmt) return false;
  }
  out.id = id;
  out.title = media["metadata"]["title"] | "";
  out.author = media["metadata"]["authorName"] | "";
  if (out.title.empty()) out.title = "(untitled)";
  return true;
}

inline void parseShelves(JsonDocument& doc, std::vector<AbsShelf>& out) {
  out.clear();
  for (JsonVariantConst shelfV : doc.as<JsonArrayConst>()) {
    AbsShelf shelf;
    shelf.id = shelfV["id"] | "";
    for (JsonVariantConst e : shelfV["entities"].as<JsonArrayConst>()) {
      if (e["books"].is<JsonArrayConst>()) {
        AbsItem series;
        series.id = e["id"] | "";
        series.title = e["name"] | "";
        series.isSeries = true;
        for (JsonVariantConst b : e["books"].as<JsonArrayConst>()) {
          AbsItem book;
          if (parseBook(b, book)) series.books.push_back(std::move(book));
        }
        if (!series.books.empty()) shelf.items.push_back(std::move(series));
      } else {
        AbsItem book;
        if (parseBook(e, book)) shelf.items.push_back(std::move(book));
      }
    }
    out.push_back(std::move(shelf));
  }
}

inline void parseSearch(JsonDocument& doc, std::vector<AbsItem>& out) {
  out.clear();
  for (JsonVariantConst r : doc["book"].as<JsonArrayConst>()) {
    AbsItem book;
    if (parseBook(r["libraryItem"], book)) out.push_back(std::move(book));
  }
}

inline void parseLibraries(JsonDocument& doc, std::vector<AbsLibrary>& out) {
  out.clear();
  for (JsonVariantConst l : doc["libraries"].as<JsonArrayConst>()) {
    AbsLibrary lib;
    lib.id = l["id"] | "";
    lib.name = l["name"] | "";
    lib.mediaType = l["mediaType"] | "";
    if (!lib.id.empty()) out.push_back(std::move(lib));
  }
}

inline std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

inline std::string pickLibraryId(const std::vector<AbsLibrary>& libs, const std::string& wantedName) {
  const std::string want = lower(wantedName);
  for (const auto& l : libs) {
    if (l.mediaType == "book" && lower(l.name) == want) return l.id;
  }
  for (const auto& l : libs) {
    if (l.mediaType == "book") return l.id;
  }
  return "";
}

inline std::string trim(const std::string& s) {
  size_t a = 0;
  size_t b = s.size();
  while (a < b && isspace(static_cast<unsigned char>(s[a]))) a++;
  while (b > a && isspace(static_cast<unsigned char>(s[b - 1]))) b--;
  return s.substr(a, b - a);
}

inline std::string normalizeServer(const std::string& raw) {
  std::string s = trim(raw);
  if (s.empty()) return s;
  if (s.find("://") == std::string::npos) s = "https://" + s;
  while (!s.empty() && s.back() == '/') s.pop_back();
  return s;
}

inline bool hasPath(const std::string& server) {
  const size_t schemeEnd = server.find("://");
  const size_t hostStart = schemeEnd == std::string::npos ? 0 : schemeEnd + 3;
  return server.find('/', hostStart) != std::string::npos;
}

inline std::string origin(const std::string& url) {
  const size_t schemeEnd = url.find("://");
  const size_t hostStart = schemeEnd == std::string::npos ? 0 : schemeEnd + 3;
  const size_t slash = url.find('/', hostStart);
  return slash == std::string::npos ? url : url.substr(0, slash);
}

inline std::string hostLabel(const std::string& server) {
  std::string o = origin(server);
  const size_t schemeEnd = o.find("://");
  return schemeEnd == std::string::npos ? o : o.substr(schemeEnd + 3);
}

inline std::string urlEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

inline std::string itemLabel(const AbsItem& item) {
  if (item.isSeries) return item.title + " (" + std::to_string(item.books.size()) + ")";
  if (item.author.empty()) return item.title;
  return item.title + " - " + item.author;
}

inline std::string sanitizeFilename(const std::string& title, const std::string& author, const std::string& id) {
  std::string base = author.empty() ? title : title + " - " + author;
  std::string out;
  for (unsigned char c : base) {
    if (c < 0x20 || strchr("\\/:*?\"<>|", c)) {
      out += '_';
    } else {
      out += static_cast<char>(c);
    }
  }
  if (out.size() > 80) {
    size_t cut = 80;
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) cut--;
    out.resize(cut);
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
  size_t lead = 0;
  while (lead < out.size() && (out[lead] == ' ' || out[lead] == '.')) lead++;
  out.erase(0, lead);
  if (out.empty()) out = id.empty() ? "book" : id;
  return out + ".epub";
}

}
