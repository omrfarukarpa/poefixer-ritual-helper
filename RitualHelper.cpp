#include "sdk/PluginSDK.h"

#include "config/Settings.h"
#include "game/DeferPlanner.h"
#include "game/DeferState.h"
#include "game/RitualScanner.h"
#include "game/RitualUi.h"
#include "net/Poe2Scout.h"
#include "overlay/DeferButtonOverlay.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

inline constexpr const char* kRitualHelperVersion    = "1.3.0";
inline constexpr const char* kRitualHelperMaintainer = "Omer Faruk ARPA";

using RitualHelperConfig::Settings;
using Clock = std::chrono::steady_clock;

class RitualHelperPlugin : public PluginSDK::Plugin {
public:
    ~RitualHelperPlugin() override {
        m_fetchAbort = true;
        if (m_fetchThread.joinable()) m_fetchThread.join();
    }

    const char* GetName() const override { return "Ritual Helper"; }

    bool WantsOverlay() const override {
        return m_settings.enabled && m_settings.showOverlay;
    }

    void OnEnable(bool) override {
        if (!HostCompatible()) {
            ctx()->Log.Error(
                "Ritual Helper: incompatible PoeFixer host (SDK version/size mismatch) - disabled");
            return;
        }
        if (ctx()->ImGuiContext)
            ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));

        m_settings.Load(DirectoryPath());
        m_loaded = true;
        m_lastScan = Clock::now() - std::chrono::milliseconds(m_settings.scanIntervalMs);
        auto& events = const_cast<PluginSDK::EventsService&>(ctx()->Events);
        m_frameTok = events.OnFrame([this] { FrameTick(); });
        StartFetch();
        ctx()->Log.Info("Ritual Helper plugin enabled");
    }

    void OnDisable() override {
        auto& events = const_cast<PluginSDK::EventsService&>(ctx()->Events);
        if (m_frameTok.Valid()) events.Unsubscribe(m_frameTok);
        m_frameTok = {};
        m_fetchAbort = true;
        if (m_fetchThread.joinable()) m_fetchThread.join();
        ctx()->Overlay.SetWantsOverlayInput(false);
        m_window.reset();
        m_uiHits.clear();
        SaveSettings();
        ctx()->Log.Info("Ritual Helper plugin disabled");
    }

    void SaveSettings() override {
        if (m_loaded) m_settings.Save(DirectoryPath());
    }

    void DrawUI() override {
        if (!m_settings.enabled || !m_settings.showOverlay) {
            ResetOverlayCapture();
            return;
        }
        if (!ctx()->Game.IsInGame()) {
            ResetOverlayCapture();
            return;
        }
        if (ctx()->ImGuiContext)
            ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));
        if (!ctx()->Game.IsForeground()) {
            ResetOverlayCapture();
            return;
        }

        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        RefreshIfNeeded(disp.x, disp.y);
        if (!m_window) {
            ResetOverlayCapture();
            m_hwClick.Reset();
            return;
        }

        ImDrawList* dl = ImGui::GetForegroundDrawList();

        const bool dryFlash = Clock::now() < m_dryFlashUntil;
        for (const auto& it : m_matches) {
            const ImVec2 a(it.rect.x + 3.f, it.rect.y + 3.f);
            const ImVec2 b(it.rect.x + it.rect.w - 3.f, it.rect.y + it.rect.h - 3.f);
            dl->AddRect(a, b, IM_COL32(255, 170, 40, 255), 0.f, 0, dryFlash ? 4.f : 2.5f);
            if (it.valueEx > 0.0) {
                char val[32];
                FormatValue(val, sizeof(val), it.valueEx);
                const ImVec2 ts = ImGui::CalcTextSize(val);
                const ImVec2 tp(a.x + 2.f, a.y + 2.f);
                dl->AddRectFilled(ImVec2(tp.x - 2.f, tp.y - 1.f),
                                  ImVec2(tp.x + ts.x + 2.f, tp.y + ts.y + 1.f),
                                  IM_COL32(0, 0, 0, 210), 2.f);
                dl->AddText(tp, IM_COL32(255, 190, 70, 255), val);
            }
        }

        DrawDeferButton();

        char status[224];
        std::snprintf(status, sizeof(status),
                      "Ritual Helper: '%s' items=%zu matched=%zu%s%s",
                      m_window->name.c_str(), m_window->items.size(), m_matches.size(),
                      m_defer.Status().empty() ? "" : " | ",
                      m_defer.Status().c_str());
        const ImVec2 pos(14.f, 58.f);
        const ImVec2 sz = ImGui::CalcTextSize(status);
        dl->AddRectFilled(ImVec2(pos.x - 4, pos.y - 2),
                          ImVec2(pos.x + sz.x + 4, pos.y + sz.y + 2),
                          IM_COL32(0, 0, 0, 200), 3.f);
        dl->AddText(pos, IM_COL32(120, 230, 255, 255), status);

        if (dryFlash) {
            char dry[128];
            std::snprintf(dry, sizeof(dry), "DRY RUN: would defer %zu item(s) - no clicks sent",
                          m_matches.size());
            dl->AddText(ImVec2(pos.x, pos.y + sz.y + 8.f),
                        IM_COL32(255, 170, 40, 255), dry);
        }
    }

    void DrawDeferButton() {
        if (m_defer.IsRunning()) {
            ResetOverlayCapture();
            m_hwClick.Reset();
            return;
        }
        if (m_matches.empty() || (!m_toggle && m_bottom.mode == RitualHelper::BottomButtonMode::None)) {
            ResetOverlayCapture();
            m_hwClick.Reset();
            return;
        }

        ImVec2 pos;
        if (m_toggle) {
            pos = ImVec2(m_toggle->x - RitualHelperOverlay::kButtonW - 12.f,
                         m_toggle->y + (m_toggle->h - RitualHelperOverlay::kButtonH) * 0.5f);
        } else {
            pos = ImVec2(m_window->gridX + static_cast<float>(m_window->totalBoxesX) * m_window->cellSize
                             - RitualHelperOverlay::kButtonW,
                         m_window->gridY - 44.f);
        }

        char label[32];
        std::snprintf(label, sizeof(label), "%s (%zu)",
                      m_settings.dryRun ? "DRY DEFER" : "DEFER", m_matches.size());

        const ImVec2 p1(pos.x + RitualHelperOverlay::kButtonW,
                        pos.y + RitualHelperOverlay::kButtonH);
        const bool over = RitualHelperOverlay::HitRect(ImGui::GetIO().MousePos, pos, p1);
        m_overlayCapturePending = over;
        ctx()->Overlay.SetWantsOverlayInput(over || m_overlayCaptureApplied);

        const auto r = RitualHelperOverlay::DrawOverlayButton(pos, label, "##ritual_defer");
        const bool hw = m_hwClick.Update(true, r.btnP0, r.btnP1);
        if (r.clicked || hw) {
            ResetOverlayCapture();
            StartDefer();
        }
    }

    void DrawSettings() override {
        if (!ctx()->ImGuiContext) return;
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));

        ImGui::TextDisabled("Ritual Helper v%s  -  by %s",
                            kRitualHelperVersion, kRitualHelperMaintainer);
        ImGui::Checkbox("Enable Ritual Helper", &m_settings.enabled);
        ImGui::SameLine();
        ImGui::Checkbox("Show overlay", &m_settings.showOverlay);

        ImGui::TextWrapped(
            "With a Favours window open, matching items get an orange outline and a "
            "DEFER button appears next to the hourglass. It enters defer mode, "
            "clicks the matched items and applies - right-click cancels a run.");

        ImGui::SliderInt("Scan interval (ms)", &m_settings.scanIntervalMs,
                         RitualHelperConfig::kScanIntervalMinMs,
                         RitualHelperConfig::kScanIntervalMaxMs);

        DrawLeaguePicker();

        DrawItemPicker();

        ImGui::SeparatorText("Value defer (poe2scout)");
        ImGui::TextWrapped("Also defer any revealed item whose live poe2scout price "
                           "is at least this value. 0 = off.");
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", m_settings.minValue);
            ImGui::SetNextItemWidth(110.f);
            if (ImGui::InputText("Min value", buf, sizeof(buf),
                                 ImGuiInputTextFlags_CharsDecimal)) {
                const int nv = buf[0] ? std::atoi(buf) : 0;
                m_settings.minValue =
                    nv < 0 ? 0 : (nv > RitualHelperConfig::kMinValueMax
                                      ? RitualHelperConfig::kMinValueMax : nv);
            }
        }
        ImGui::SameLine();
        static const char* kUnits[] = {"Exalted", "Divine"};
        ImGui::SetNextItemWidth(100.f);
        ImGui::Combo("##valunit", &m_settings.minValueUnit, kUnits, 2);
        {
            std::lock_guard<std::mutex> lk(m_fetchMutex);
            if (m_settings.minValue > 0 && m_prices.divinePrice > 0.0) {
                ImGui::SameLine();
                if (m_settings.minValueUnit == RitualHelperConfig::kValueUnitDivine)
                    ImGui::TextDisabled("= %.0f ex",
                                        m_settings.minValue * m_prices.divinePrice);
                else
                    ImGui::TextDisabled("= %.2f div",
                                        m_settings.minValue / m_prices.divinePrice);
            }
            ImGui::TextDisabled("Prices: %s", m_fetchStatus.c_str());
        }
        ImGui::SliderInt("Auto refresh (minutes)", &m_settings.priceRefreshMinutes,
                         RitualHelperConfig::kRefreshMinMinutes,
                         RitualHelperConfig::kRefreshMaxMinutes);
        if (ImGui::Button(m_fetching ? "Refreshing..." : "Refresh now") && !m_fetching)
            StartFetch();

        ImGui::SeparatorText("Debug");
        ImGui::Checkbox("Debug mode", &m_settings.debugMode);
        if (m_settings.debugMode) {
            ImGui::Checkbox("Dry run (no clicks, just show what would be deferred)",
                            &m_settings.dryRun);

            if (m_window) {
                ImGui::Text("Window: '%s' (id=%d, %dx%d), items=%zu",
                            m_window->name.c_str(), m_window->inventoryId,
                            m_window->totalBoxesX, m_window->totalBoxesY,
                            m_window->items.size());
            } else {
                ImGui::TextDisabled("No ritual window detected (open the Favours window).");
            }
            ImGui::Text("Ritual-keyword UI elements: %zu", m_uiHits.size());
            int shown = 0;
            for (const auto& e : m_uiHits) {
                if (shown++ >= 10) { ImGui::TextDisabled("..."); break; }
                ImGui::TextDisabled("  id='%s' text='%s'", e.stringId.c_str(), e.text.c_str());
            }

            if (ImGui::Button("Write ritual dump")) WriteDump();
            if (!m_lastDumpPath.empty())
                ImGui::TextDisabled("Last dump: %s", m_lastDumpPath.c_str());
        }
    }

    void DrawItemPicker() {
        std::lock_guard<std::mutex> lk(m_fetchMutex);

        char header[96];
        std::snprintf(header, sizeof(header), "Defer items (%zu selected)###defer_items",
                      m_settings.selectedItems.size());
        ImGui::SeparatorText(header);

        if (m_prices.categories.empty()) {
            ImGui::TextDisabled("Item list loads from poe2scout - %s", m_fetchStatus.c_str());
            return;
        }

        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s", m_itemSearch.c_str());
        ImGui::SetNextItemWidth(280.f);
        if (ImGui::InputTextWithHint("##itemsearch", "search items (e.g. omen, mageblood)...",
                                     buf, sizeof(buf)))
            m_itemSearch = buf;
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear selected")) m_settings.selectedItems.clear();

        ImGui::BeginChild("deferitemlist", ImVec2(0.f, 220.f), ImGuiChildFlags_Borders);
        for (const auto& cat : m_prices.categories) {
            bool headerShown = false;
            for (const auto& name : cat.second) {
                if (!m_itemSearch.empty()
                    && !RitualHelper::ContainsCI(name, m_itemSearch.c_str()))
                    continue;
                if (!headerShown) {
                    ImGui::SeparatorText(cat.first.c_str());
                    headerShown = true;
                }
                bool on = false;
                for (const auto& s : m_settings.selectedItems)
                    if (s == name) { on = true; break; }
                ImGui::PushID(name.c_str());
                if (ImGui::Checkbox(name.c_str(), &on)) {
                    if (on) {
                        m_settings.selectedItems.push_back(name);
                    } else {
                        m_settings.selectedItems.erase(
                            std::remove(m_settings.selectedItems.begin(),
                                        m_settings.selectedItems.end(), name),
                            m_settings.selectedItems.end());
                    }
                }
                auto pIt = m_prices.priceExalted.find(name);
                if (pIt != m_prices.priceExalted.end() && pIt->second > 0.0) {
                    ImGui::SameLine();
                    char val[32];
                    FormatValueLocked(val, sizeof(val), pIt->second);
                    ImGui::TextDisabled("(%s)", val);
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    void DrawLeaguePicker() {
        std::lock_guard<std::mutex> lk(m_fetchMutex);
        ImGui::SeparatorText("Price league");

        std::vector<const char*> labels;
        labels.reserve(m_prices.leagues.size() + 1);
        labels.push_back("Auto (current league)");
        for (const auto& name : m_prices.leagues) labels.push_back(name.c_str());

        int selected = 0;
        if (!m_settings.league.empty()) {
            for (size_t i = 0; i < m_prices.leagues.size(); ++i) {
                if (m_prices.leagues[i] == m_settings.league) {
                    selected = static_cast<int>(i + 1);
                    break;
                }
            }
        }
        ImGui::SetNextItemWidth(280.f);
        if (labels.size() == 1) {
            ImGui::TextDisabled("League list loads from poe2scout - %s", m_fetchStatus.c_str());
            return;
        }
        if (ImGui::Combo("Season / league", &selected, labels.data(),
                         static_cast<int>(labels.size()))) {
            const std::string next = selected > 0
                ? m_prices.leagues[static_cast<size_t>(selected - 1)] : std::string();
            if (next != m_settings.league) {
                m_settings.league = next;
                m_fetchAgain = true;
                m_fetchAbort = true;
            }
        }
        ImGui::TextDisabled("Selected: %s", m_prices.league.empty()
                            ? "auto" : m_prices.league.c_str());
    }


private:
    Settings m_settings;
    bool m_loaded = false;

    std::optional<RitualHelper::RitualWindow> m_window;
    std::vector<RitualHelper::UiElement> m_uiHits;
    std::vector<RitualHelper::RitualItem> m_matches;
    RitualHelper::BottomButton m_bottom;
    std::optional<RitualHelper::UiElement> m_toggle;
    RitualHelper::DeferState m_defer;
    RitualHelperOverlay::HardwareClick m_hwClick;
    PluginSDK::EventsService::Token m_frameTok;
    Clock::time_point m_lastScan{};
    Clock::time_point m_lastBottomPoll{};
    Clock::time_point m_dryFlashUntil{};
    std::string m_lastDumpPath;
    std::string m_itemSearch;

    std::thread m_fetchThread;
    std::atomic<bool> m_fetching{false};
    std::atomic<bool> m_fetchAbort{false};
    std::atomic<bool> m_fetchAgain{false};
    bool m_overlayCapturePending = false;
    bool m_overlayCaptureApplied = false;
    std::mutex m_fetchMutex;
    RitualHelper::PriceResult m_prices;
    std::string m_fetchStatus = "not fetched yet";
    Clock::time_point m_nextAutoFetch{};

    void StartFetch() {
        if (m_fetching.exchange(true)) return;
        if (m_fetchThread.joinable()) m_fetchThread.join();
        m_fetchAbort = false;
        m_nextAutoFetch = Clock::now()
                          + std::chrono::minutes(m_settings.priceRefreshMinutes);
        {
            std::lock_guard<std::mutex> lk(m_fetchMutex);
            m_fetchStatus = "fetching...";
        }
        m_fetchThread = std::thread([this] {
            std::string league;
            {
                std::lock_guard<std::mutex> lk(m_fetchMutex);
                league = m_settings.league;
            }
            RitualHelper::PriceResult r;
            try {
                r = RitualHelper::Poe2Scout::FetchAll(league, &m_fetchAbort);
            } catch (...) {
                r.status = "Price refresh failed";
            }
            {
                std::lock_guard<std::mutex> lk(m_fetchMutex);
                m_fetchStatus = r.status;
                if (r.ok && r.uniqueComplete && !m_fetchAbort.load()) {
                    m_prices = std::move(r);
                } else if (m_fetchAbort.load()) {
                    m_fetchStatus = "refresh canceled";
                } else {
                    m_fetchStatus = m_prices.categories.empty()
                        ? "Incomplete price data; retry with Refresh now"
                        : "Incomplete price data; keeping previous catalog";
                }
            }
            m_fetching = false;
        });
    }

    void FormatValueLocked(char* out, size_t n, double valueEx) {
        const double divPrice = m_prices.divinePrice;
        if (divPrice > 0.0 && valueEx >= divPrice * 0.95)
            std::snprintf(out, n, "%.1f div", valueEx / divPrice);
        else if (valueEx >= 10.0)
            std::snprintf(out, n, "%.0f ex", valueEx);
        else
            std::snprintf(out, n, "%.2f ex", valueEx);
    }

    void FormatValue(char* out, size_t n, double valueEx) {
        std::lock_guard<std::mutex> lk(m_fetchMutex);
        FormatValueLocked(out, n, valueEx);
    }

    void FrameTick() {
        if (!m_fetching && (m_fetchAgain.exchange(false) || Clock::now() > m_nextAutoFetch))
            StartFetch();
        if (!m_settings.enabled || !m_settings.showOverlay || !ctx()->Game.IsInGame()
            || !ctx()->Game.IsForeground()) {
            ResetOverlayCapture();
        } else {
            m_overlayCaptureApplied = m_overlayCapturePending;
            ctx()->Overlay.SetWantsOverlayInput(m_overlayCaptureApplied);
        }
        if (!m_defer.IsRunning()) return;
        const auto now = Clock::now();
        if (m_window && now - m_lastBottomPoll > std::chrono::milliseconds(100)) {
            m_lastBottomPoll = now;
            const auto texts = RitualHelper::CollectUiTexts(ctx());
            m_bottom = RitualHelper::FindBottomButton(texts, *m_window);
        }
        m_defer.Tick(m_bottom, ctx()->Game.IsForeground());
    }

    void ResetOverlayCapture() {
        m_overlayCapturePending = false;
        m_overlayCaptureApplied = false;
        ctx()->Overlay.SetWantsOverlayInput(false);
    }

    void StartDefer() {
        if (!m_window || m_matches.empty()) return;

        if (m_settings.dryRun) {
            std::string names;
            for (const auto& it : m_matches) {
                if (!names.empty()) names += ", ";
                names += it.name;
            }
            ctx()->Log.Info(("[Ritual Helper] DRY RUN - would defer: " + names).c_str());
            m_dryFlashUntil = Clock::now() + std::chrono::milliseconds(3000);
            return;
        }

        const bool alreadyDeferMode =
            m_bottom.mode == RitualHelper::BottomButtonMode::DeferItem;
        if (!alreadyDeferMode && !m_toggle) {
            ctx()->Log.Warn("[Ritual Helper] defer toggle button not found - aborted");
            return;
        }

        RitualHelper::ScreenRect toggle;
        if (m_toggle)
            toggle = RitualHelper::ScreenRect{m_toggle->x, m_toggle->y, m_toggle->w, m_toggle->h};

        std::vector<RitualHelper::ScreenRect> rects;
        rects.reserve(m_matches.size());
        for (const auto& it : m_matches) rects.push_back(it.rect);

        m_defer.Start(alreadyDeferMode, toggle, std::move(rects));
    }

    void RefreshIfNeeded(float w, float h) {
        const auto now = Clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastScan).count();
        if (elapsed < m_settings.scanIntervalMs) return;
        m_lastScan = now;

        m_window = RitualHelper::FindRitualWindow(ctx(), w, h);
        if (m_window) {
            const auto all = RitualHelper::CollectUiTexts(ctx(), true);
            m_uiHits = RitualHelper::FindRitualUiElements(all);
            m_bottom = RitualHelper::FindBottomButton(all, *m_window);
            m_toggle = RitualHelper::FindDeferToggle(all, *m_window);
            {
                std::lock_guard<std::mutex> lk(m_fetchMutex);
                double minEx = static_cast<double>(m_settings.minValue);
                if (m_settings.minValueUnit == RitualHelperConfig::kValueUnitDivine)
                    minEx = m_prices.divinePrice > 0.0
                                ? minEx * m_prices.divinePrice : 0.0;
                m_matches = RitualHelper::MatchDeferItems(
                    *m_window, m_settings.selectedItems,
                    m_prices.priceExalted, minEx);
            }
        } else {
            m_uiHits.clear();
            m_matches.clear();
            m_bottom = {};
            m_toggle.reset();
        }
    }

    void WriteDump() {
        const ImVec2 disp = ImGui::GetIO().DisplaySize;

        std::string s = "[Ritual Helper] discovery dump\n\n";
        s += RitualHelper::BuildInventoryDump(ctx(), disp.x, disp.y);
        s += "\n";
        const auto all = RitualHelper::CollectUiTexts(ctx(), true);
        const auto win = RitualHelper::FindRitualWindow(ctx(), disp.x, disp.y);
        if (win && win->cellSize > 0.f) {
            const float headerPad = 280.f;
            s += RitualHelper::BuildRegionDump(
                all, win->gridX, win->gridY - headerPad,
                static_cast<float>(win->totalBoxesX) * win->cellSize,
                static_cast<float>(win->totalBoxesY) * win->cellSize + headerPad);
            s += "\n";
        }
        s += RitualHelper::BuildUiDump(all);

        try {
            const auto dir = DirectoryPath() / "debug";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            const auto path = dir / "ritual-dump.txt";
            std::ofstream out(path);
            if (out.is_open()) out << s;
            m_lastDumpPath = path.string();
            ctx()->Log.Info(("[Ritual Helper] dump written to " + m_lastDumpPath).c_str());
        } catch (...) {}
    }
};

extern "C" PLUGIN_API PluginSDK::Plugin* CreatePlugin() { return new RitualHelperPlugin(); }

extern "C" PLUGIN_API void DestroyPlugin(PluginSDK::Plugin* p) { delete p; }
