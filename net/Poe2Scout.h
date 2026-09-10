#pragma once

#include "../third_party/json.hpp"
#include "../config/UniqueCatalog.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <Windows.h>
#include <winhttp.h>

namespace RitualHelper {

enum class Category : int {
    Currency = 0,
    Essence,
    Rune,
    SoulCore,
    Catalyst,
    Delirium,
    Incursion,
    Idol,
    Ritual,
    VaultKey,
    Fragment,
    Abyss,
    UncutGem,
    LineageGem,
    Verisium,
    Vaal,
    Expedition,
    UniqueWeapon,
    UniqueArmour,
    UniqueAccessory,
    UniqueJewel,
    UniqueFlask,
    UniqueSanctum,
    Count
};

inline constexpr int kCategoryCount = static_cast<int>(Category::Count);

inline const char* CategoryName(Category c) {
    switch (c) {
        case Category::Currency:        return "Currency";
        case Category::Essence:         return "Essences";
        case Category::Rune:            return "Runes";
        case Category::SoulCore:        return "Soul Cores (Ultimatum)";
        case Category::Catalyst:        return "Catalysts (Breach)";
        case Category::Delirium:        return "Delirium (Liquid)";
        case Category::Incursion:       return "Incursion";
        case Category::Idol:            return "Idols";
        case Category::Ritual:          return "Ritual (Omens)";
        case Category::VaultKey:        return "Vault Keys";
        case Category::Fragment:        return "Fragments";
        case Category::Abyss:           return "Abyss";
        case Category::UncutGem:        return "Uncut Gems";
        case Category::LineageGem:      return "Lineage Gems";
        case Category::Verisium:        return "Verisium / Alloys";
        case Category::Vaal:            return "Vaal";
        case Category::Expedition:      return "Expedition";
        case Category::UniqueWeapon:    return "Unique Weapons";
        case Category::UniqueArmour:    return "Unique Armour";
        case Category::UniqueAccessory: return "Unique Accessories";
        case Category::UniqueJewel:     return "Unique Jewels";
        case Category::UniqueFlask:     return "Unique Flasks";
        case Category::UniqueSanctum:   return "Unique Sanctum";
        default:                        return "?";
    }
}

inline Category CatFromApiId(const std::string& a) {
    if (a == "currency")           return Category::Currency;
    if (a == "essences")           return Category::Essence;
    if (a == "runes")              return Category::Rune;
    if (a == "ultimatum")          return Category::SoulCore;
    if (a == "expedition")         return Category::Expedition;
    if (a == "ritual")             return Category::Ritual;
    if (a == "vaultkeys")          return Category::VaultKey;
    if (a == "breach")             return Category::Catalyst;
    if (a == "abyss")              return Category::Abyss;
    if (a == "uncutgems")          return Category::UncutGem;
    if (a == "lineagesupportgems") return Category::LineageGem;
    if (a == "delirium")           return Category::Delirium;
    if (a == "incursion")          return Category::Incursion;
    if (a == "idol")               return Category::Idol;
    if (a == "verisium")           return Category::Verisium;
    if (a == "vaal")               return Category::Vaal;
    if (a == "fragments")          return Category::Fragment;
    return Category::Count;
}

inline Category CatFromUniqueApiId(const std::string& a) {
    if (a == "weapon" || a == "unique weapon")       return Category::UniqueWeapon;
    if (a == "armour" || a == "unique armour")       return Category::UniqueArmour;
    if (a == "accessory" || a == "unique accessory") return Category::UniqueAccessory;
    if (a == "jewel" || a == "unique jewel")         return Category::UniqueJewel;
    if (a == "flask" || a == "unique flask")         return Category::UniqueFlask;
    if (a == "sanctum" || a == "unique sanctum")     return Category::UniqueSanctum;
    return Category::Count;
}

struct PriceResult {
    bool ok = false;
    std::string status;
    std::string league;
    double divinePrice = 0.0;
    double relExalted = 1.0;
    double relChaos = 0.0;
    double relDivine = 0.0;
    std::unordered_map<std::string, double> priceExalted;
    std::array<std::vector<std::string>, kCategoryCount> categories;
    std::vector<std::string> leagues;
    size_t uniqueItems = 0;
    bool uniqueComplete = false;

    bool HasCategories() const {
        for (const auto& c : categories) {
            if (!c.empty()) return true;
        }
        return false;
    }
};

class Poe2Scout {
public:
    static PriceResult FetchAll(const std::string& requestedLeague = {},
                                const std::atomic<bool>* abort = nullptr) {
        PriceResult r;
        if (Aborted(abort)) return r;
        DetectLeague(r, requestedLeague, abort);
        if (r.league.empty()) r.league = "Runes of Aldur";

        std::string refBody;
        if (Get("/api/poe2/Leagues/" + Encode(r.league) + "/ReferenceCurrencies", refBody, abort))
            ParseReference(refBody, r);

        if (r.relDivine > 0.0)
            r.divinePrice = r.relDivine;

        SeedUniqueCatalog(r);

        static const char* kCurrencyCats[] = {
            "currency", "essences", "runes", "ultimatum", "expedition", "ritual", "vaultkeys",
            "breach", "abyss", "uncutgems", "lineagesupportgems", "delirium", "incursion", "idol",
            "verisium", "vaal", "fragments",
        };
        int okCount = 0;
        for (const char* catStr : kCurrencyCats) {
            if (Aborted(abort)) break;
            std::string body;
            const std::string path = "/api/poe2/Leagues/" + Encode(r.league) +
                                     "/Currencies/ByCategory?Category=" + catStr +
                                     "&PerPage=250&Page=1";
            const Category cat = CatFromApiId(catStr);
            if (Get(path, body, abort) && ParseItems(body, "Text", cat, r)) ++okCount;
        }

        static const char* kUniqueCats[] = {
            "weapon", "armour", "accessory", "jewel", "flask", "sanctum",
        };
        int uniqueCount = 0;
        bool uniqueComplete = true;
        for (const char* catStr : kUniqueCats) {
            if (Aborted(abort)) break;
            const Category cat = CatFromUniqueApiId(catStr);
            bool categoryComplete = false;
            for (int page = 1; page <= 4; ++page) {
                std::string body;
                const std::string path = "/api/poe2/Leagues/" + Encode(r.league) +
                                         "/Uniques/ByCategory?Category=" + std::string(catStr) +
                                         "&PerPage=250&Page=" + std::to_string(page);
                if (!Get(path, body, abort)) { uniqueComplete = false; break; }
                int pages = 0;
                if (!ParseItems(body, "Name", cat, r, &pages)) {
                    uniqueComplete = false;
                    break;
                }
                ++uniqueCount;
                if (page >= pages) {
                    if (cat != Category::Count)
                        categoryComplete = !r.categories[static_cast<size_t>(cat)].empty();
                    break;
                }
            }
            if (!categoryComplete) uniqueComplete = false;
        }

        for (int i = static_cast<int>(Category::UniqueWeapon); i <= static_cast<int>(Category::UniqueSanctum); ++i) {
            r.uniqueItems += r.categories[i].size();
        }
        r.uniqueComplete = uniqueComplete && r.uniqueItems > 0 && !Aborted(abort);

        for (auto& vec : r.categories) {
            std::sort(vec.begin(), vec.end(), [&r](const std::string& a,
                                                              const std::string& b) {
                const auto ia = r.priceExalted.find(a);
                const auto ib = r.priceExalted.find(b);
                const double pa = ia != r.priceExalted.end() ? ia->second : 0.0;
                const double pb = ib != r.priceExalted.end() ? ib->second : 0.0;
                if (pa != pb) return pa > pb;
                return a < b;
            });
        }

        r.ok = okCount > 0;
        if (!r.ok) {
            r.status = "Fetch failed";
        } else {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "Updated (%s, %d currency cats, %d unique pages, %zu prices)",
                          r.league.c_str(), okCount, uniqueCount, r.priceExalted.size());
            r.status = buf;
        }
        return r;
    }

private:
    static bool Aborted(const std::atomic<bool>* abort) {
        return abort && abort->load();
    }

    static std::string Encode(const std::string& s) {
        std::string o;
        for (char c : s) {
            if (c == ' ') o += "%20";
            else o += c;
        }
        return o;
    }

    static std::wstring Widen(const std::string& s) {
        std::wstring w;
        w.reserve(s.size());
        for (char c : s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        return w;
    }

    static bool Get(const std::string& path, std::string& out,
                    const std::atomic<bool>* abort = nullptr) {
        out.clear();
        if (Aborted(abort)) return false;
        HINTERNET hSession = WinHttpOpen(L"RitualHelper/1.0",
                                         WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;
        WinHttpSetTimeouts(hSession, 4000, 6000, 8000, 10000);

        bool ok = false;
        HINTERNET hConnect = WinHttpConnect(hSession, L"poe2scout.com",
                                            INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (hConnect) {
            const std::wstring wpath = Widen(path);
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(), nullptr,
                                                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                    WINHTTP_FLAG_SECURE);
            if (hRequest) {
                if (!Aborted(abort) &&
                    WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                    WinHttpReceiveResponse(hRequest, nullptr) && !Aborted(abort)) {
                    DWORD status = 0;
                    DWORD statusSize = sizeof(status);
                    const bool httpOk = WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                        WINHTTP_NO_HEADER_INDEX) && status == 200;
                    while (httpOk && !Aborted(abort)) {
                        DWORD avail = 0;
                        if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
                        if (avail == 0) { ok = !out.empty(); break; }
                        std::string chunk(avail, '\0');
                        DWORD read = 0;
                        if (!WinHttpReadData(hRequest, chunk.data(), avail, &read)) break;
                        if (read == 0) break;
                        chunk.resize(read);
                        out += chunk;
                    }
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
        return ok && !Aborted(abort);
    }

    static void DetectLeague(PriceResult& r, const std::string& requestedLeague,
                             const std::atomic<bool>* abort) {
        std::string body;
        if (!Get("/api/poe2/Leagues", body, abort)) return;
        nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
        if (j.is_discarded() || !j.is_array()) return;
        std::string current;
        double currentDivinePrice = 0.0;
        bool requestedFound = false;
        for (const auto& e : j) {
            if (!e.is_object()) continue;
            const std::string shortName = e.value("ShortName", std::string());
            if (shortName.size() >= 2 && shortName.compare(shortName.size() - 2, 2, "hc") == 0)
                continue;
            const std::string value = e.value("Value", std::string());
            if (value.rfind("HC ", 0) == 0) continue;
            if (value.empty()) continue;
            r.leagues.push_back(value);
            if (e.value("IsCurrent", false) && current.empty()) {
                current = value;
                currentDivinePrice = e.value("DivinePrice", 0.0);
                if (requestedLeague.empty()) r.divinePrice = currentDivinePrice;
            }
            if (!requestedLeague.empty() && value == requestedLeague) {
                requestedFound = true;
                r.divinePrice = e.value("DivinePrice", 0.0);
            }
        }
        if (!requestedLeague.empty() && requestedFound) {
            r.league = requestedLeague;
        } else {
            r.league = current;
            r.divinePrice = currentDivinePrice;
        }
        if (r.league == requestedLeague && r.divinePrice <= 0.0) {
            for (const auto& e : j) {
                if (!e.is_object() || e.value("Value", std::string()) != r.league) continue;
                r.divinePrice = e.value("DivinePrice", 0.0);
                break;
            }
        }
    }

    static void ParseReference(const std::string& body, PriceResult& r) {
        nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
        if (j.is_discarded() || !j.is_array()) return;
        for (const auto& e : j) {
            if (!e.is_object()) continue;
            const std::string id = e.value("ApiId", std::string());
            const double rel = e.value("RelativePrice", 0.0);
            if (id == "exalted") r.relExalted = rel;
            else if (id == "chaos") r.relChaos = rel;
            else if (id == "divine") r.relDivine = rel;
        }
    }

    static void SeedUniqueCatalog(PriceResult& r) {
        for (size_t i = 0; i < kUniqueCatalogCount; ++i) {
            const auto& entry = kUniqueCatalog[i];
            const Category cat = CatFromUniqueApiId(entry.category);
            if (cat == Category::Count) continue;
            auto& vec = r.categories[static_cast<size_t>(cat)];
            if (std::find(vec.begin(), vec.end(), entry.name) == vec.end()) {
                vec.push_back(entry.name);
            }
        }
    }

    static bool ParseItems(const std::string& body, const char* nameKey, Category cat,
                           PriceResult& r, int* pagesOut = nullptr) {
        if (cat == Category::Count) return false;
        nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("Items")) return false;
        if (pagesOut) *pagesOut = j.value("Pages", 1);
        const auto& items = j["Items"];
        if (!items.is_array()) return false;

        auto& names = r.categories[static_cast<size_t>(cat)];
        for (const auto& it : items) {
            if (!it.is_object()) continue;
            const std::string name = it.value(nameKey, std::string());
            if (name.empty()) continue;
            if (std::find(names.begin(), names.end(), name) == names.end())
                names.push_back(name);
            if (it.contains("CurrentPrice") && it["CurrentPrice"].is_number())
                r.priceExalted[name] = it["CurrentPrice"].get<double>();
        }
        return true;
    }
};

}
