#pragma once

#include "../third_party/json.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Windows.h>
#include <winhttp.h>

namespace RitualHelper {

struct PriceResult {
    bool ok = false;
    std::string status;
    std::string league;
    double divinePrice = 0.0;
    std::unordered_map<std::string, double> priceExalted;
    std::vector<std::pair<std::string, std::vector<std::string>>> categories;
    std::vector<std::string> leagues;
};

class Poe2Scout {
public:
    static PriceResult FetchAll(const std::string& requestedLeague = {},
                                const std::atomic<bool>* abort = nullptr) {
        PriceResult r;
        if (Aborted(abort)) return r;
        DetectLeague(r, requestedLeague, abort);
        if (r.league.empty()) r.league = "Runes of Aldur";

        static const char* kCurrencyCats[] = {
            "currency", "essences", "runes", "ultimatum", "expedition", "ritual", "vaultkeys",
            "breach", "abyss", "uncutgems", "lineagesupportgems", "delirium", "incursion", "idol",
            "verisium", "vaal", "fragments",
        };
        int okCount = 0;
        for (const char* cat : kCurrencyCats) {
            if (Aborted(abort)) break;
            std::string body;
            const std::string path = "/api/poe2/Leagues/" + Encode(r.league) +
                                     "/Currencies/ByCategory?Category=" + cat +
                                     "&PerPage=250&Page=1";
            if (Get(path, body, abort) && ParseItems(body, "Text", cat, r)) ++okCount;
        }

        static const char* kUniqueCats[] = {
            "weapon", "armour", "accessory", "jewel", "flask", "sanctum",
        };
        int uniqueCount = 0;
        for (const char* cat : kUniqueCats) {
            if (Aborted(abort)) break;
            const std::string label = std::string("unique ") + cat;
            for (int page = 1; page <= 4; ++page) {
                std::string body;
                const std::string path = "/api/poe2/Leagues/" + Encode(r.league) +
                                         "/Uniques/ByCategory?Category=" + std::string(cat) +
                                         "&PerPage=250&Page=" + std::to_string(page);
                if (!Get(path, body, abort)) break;
                int pages = 0;
                if (!ParseItems(body, "Name", label.c_str(), r, &pages)) break;
                ++uniqueCount;
                if (page >= pages) break;
            }
        }

        for (auto& c : r.categories)
            std::sort(c.second.begin(), c.second.end());

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
                    DWORD avail = 0;
                    do {
                        if (Aborted(abort)) break;
                        avail = 0;
                        if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
                        if (avail == 0) break;
                        std::string chunk(avail, '\0');
                        DWORD read = 0;
                        if (!WinHttpReadData(hRequest, chunk.data(), avail, &read)) break;
                        chunk.resize(read);
                        out += chunk;
                    } while (avail > 0);
                    ok = !out.empty();
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
        return ok;
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
        if (!requestedLeague.empty() && requestedFound) r.league = requestedLeague;
        else {
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

    static bool ParseItems(const std::string& body, const char* nameKey, const char* category,
                           PriceResult& r, int* pagesOut = nullptr) {
        nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("Items")) return false;
        if (pagesOut) *pagesOut = j.value("Pages", 1);
        const auto& items = j["Items"];
        if (!items.is_array()) return false;

        std::vector<std::string>* names = nullptr;
        for (auto& c : r.categories)
            if (c.first == category) { names = &c.second; break; }
        if (!names) {
            r.categories.emplace_back(category, std::vector<std::string>());
            names = &r.categories.back().second;
        }

        for (const auto& it : items) {
            if (!it.is_object()) continue;
            const std::string name = it.value(nameKey, std::string());
            if (name.empty()) continue;
            names->push_back(name);
            if (it.contains("CurrentPrice") && it["CurrentPrice"].is_number())
                r.priceExalted[name] = it["CurrentPrice"].get<double>();
        }
        return true;
    }
};

}
