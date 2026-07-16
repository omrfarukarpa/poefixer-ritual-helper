#pragma once

#include "sdk/PluginSDK.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace RitualHelper {

inline char LowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool ContainsCI(const std::string& hay, const char* needle) {
    size_t nlen = 0;
    while (needle[nlen] != '\0') ++nlen;
    if (nlen == 0 || nlen > hay.size()) return false;
    for (size_t i = 0; i + nlen <= hay.size(); ++i) {
        size_t j = 0;
        for (; j < nlen; ++j)
            if (LowerAscii(hay[i + j]) != LowerAscii(needle[j])) break;
        if (j == nlen) return true;
    }
    return false;
}

struct UiElement {
    std::string text;
    std::string stringId;
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    int depth = 0;
    int childCount = 0;
};

inline std::vector<UiElement> CollectUiTexts(const PluginSDK::Context* ctx,
                                             bool includeTextless = false) {
    std::vector<UiElement> out;
    if (!ctx) return out;
    const uintptr_t root = ctx->Ui.GetGameUiRoot();
    if (!root) return out;

    std::vector<std::pair<uintptr_t, int>> stack;
    stack.push_back({root, 0});
    int visited = 0;
    while (!stack.empty() && visited < 8000) {
        const auto node = stack.back();
        stack.pop_back();
        ++visited;
        const uintptr_t addr = node.first;
        const int depth = node.second;
        if (!addr || depth > 32) continue;
        if (!ctx->Ui.IsVisible(addr)) continue;

        const auto children = ctx->Ui.GetChildren(addr);
        std::string t = ctx->Ui.GetText(addr);
        std::string sid = ctx->Ui.GetStringId(addr);
        const bool hasContent = (!t.empty() && t.size() < 160) || !sid.empty();
        if (hasContent || includeTextless) {
            UiElement e;
            e.text = std::move(t);
            e.stringId = std::move(sid);
            ctx->Ui.ComputeScreenRect(addr, e.x, e.y, e.w, e.h);
            e.depth = depth;
            e.childCount = static_cast<int>(children.size());
            if (hasContent || (e.w > 0.f && e.h > 0.f))
                out.push_back(std::move(e));
        }
        for (const uintptr_t c : children)
            if (c) stack.push_back({c, depth + 1});
    }
    return out;
}

inline const char* const* RitualKeywords(size_t& count) {
    static const char* kWords[] = {"ritual", "tribute", "defer", "reroll",
                                   "favour", "favor", "offer", "cancel", "confirm"};
    count = sizeof(kWords) / sizeof(kWords[0]);
    return kWords;
}

inline bool MatchesRitualKeyword(const UiElement& e) {
    size_t n = 0;
    const char* const* words = RitualKeywords(n);
    for (size_t i = 0; i < n; ++i) {
        if (ContainsCI(e.text, words[i])) return true;
        if (ContainsCI(e.stringId, words[i])) return true;
    }
    return false;
}

inline std::vector<UiElement> FindRitualUiElements(const std::vector<UiElement>& all) {
    std::vector<UiElement> out;
    for (const auto& e : all)
        if (MatchesRitualKeyword(e)) out.push_back(e);
    return out;
}

inline std::string BuildRegionDump(const std::vector<UiElement>& all,
                                   float rx, float ry, float rw, float rh) {
    std::string s = "=== elements inside ritual window region ===\n";
    char line[384];
    int shown = 0;
    const float margin = 90.f;
    for (const auto& e : all) {
        if (e.w <= 0.f || e.h <= 0.f) continue;
        if (e.w > rw + 2.f * margin || e.h > rh + 2.f * margin) continue;
        const float cx = e.x + e.w * 0.5f;
        const float cy = e.y + e.h * 0.5f;
        if (cx < rx - margin || cx > rx + rw + margin) continue;
        if (cy < ry - margin || cy > ry + rh + margin) continue;
        std::snprintf(line, sizeof(line),
                      "d=%d kids=%d rect=%.0f,%.0f %.0fx%.0f id='%s' text='%s'\n",
                      e.depth, e.childCount, e.x, e.y, e.w, e.h,
                      e.stringId.c_str(), e.text.c_str());
        s += line;
        if (++shown >= 220) { s += "(more truncated)\n"; break; }
    }
    if (shown == 0) s += "(none)\n";
    return s;
}

inline std::string BuildUiDump(const std::vector<UiElement>& all) {
    std::string s = "=== ritual-keyword UI elements ===\n";
    char line[384];
    int hits = 0;
    for (const auto& e : all) {
        if (!MatchesRitualKeyword(e)) continue;
        std::snprintf(line, sizeof(line),
                      "d=%d kids=%d rect=%.0f,%.0f %.0fx%.0f id='%s' text='%s'\n",
                      e.depth, e.childCount, e.x, e.y, e.w, e.h,
                      e.stringId.c_str(), e.text.c_str());
        s += line;
        if (++hits >= 80) { s += "(more hits truncated)\n"; break; }
    }
    if (hits == 0) s += "(none)\n";

    s += "\n=== all visible texted elements (bounded) ===\n";
    int shown = 0;
    for (const auto& e : all) {
        if (e.text.empty()) continue;
        std::snprintf(line, sizeof(line), "d=%d rect=%.0f,%.0f id='%s' text='%s'\n",
                      e.depth, e.x, e.y, e.stringId.c_str(), e.text.c_str());
        s += line;
        if (++shown >= 200) { s += "(more truncated)\n"; break; }
    }
    return s;
}

}
