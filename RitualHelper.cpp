#include "sdk/PluginSDK.h"

#include "config/Settings.h"
#include "game/DeferPlanner.h"
#include "game/DeferState.h"
#include "game/RitualScanner.h"
#include "game/RitualUi.h"
#include "net/Poe2Scout.h"
#include "overlay/DeferButtonOverlay.h"

#include <imgui.h>

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

inline constexpr const char* kRitualHelperVersion    = "0.3.0";
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
        if (!m_settings.enabled || !m_settings.showOverlay) return;
        if (!ctx()->Game.IsInGame()) return;
        if (ctx()->ImGuiContext)
            ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));
        if (!ctx()->Game.IsForeground()) return;

        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        RefreshIfNeeded(disp.x, disp.y);
        if (!m_window) {
            ctx()->Overlay.SetWantsOverlayInput(false);
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
            ctx()->Overlay.SetWantsOverlayInput(false);
            m_hwClick.Reset();
            return;
        }
        if (m_matches.empty() || (!m_toggle && m_bottom.mode == RitualHelper::BottomButtonMode::None)) {
            ctx()->Overlay.SetWantsOverlayInput(false);
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
        ctx()->Overlay.SetWantsOverlayInput(over);

        const auto r = RitualHelperOverlay::DrawOverlayButton(pos, label, "##ritual_defer");
        const bool hw = m_hwClick.Update(true, r.btnP0, r.btnP1);
        if (r.clicked || hw) StartDefer();
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

        ImGui::SeparatorText("Defer rules");
        ImGui::Checkbox("Dry run (no clicks, just show what would be deferred)",
                        &m_settings.dryRun);
        if (m_settings.dryRun) {
            ImGui::TextDisabled("Safe mode: the DEFER button only highlights + logs.");
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 190, 90, 255));
            ImGui::TextWrapped("Live mode: the DEFER button sends real clicks (enters "
                               "defer mode, clicks matched items, applies).");
            ImGui::PopStyleColor();
        }

        ImGui::TextDisabled("An item is deferred when its name contains any rule text:");
        int removeAt = -1;
        for (int i = 0; i < static_cast<int>(m_settings.deferRules.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::SmallButton("X")) removeAt = i;
            ImGui::SameLine();
            ImGui::TextUnformatted(m_settings.deferRules[i].c_str());
            ImGui::PopID();
        }
        if (removeAt >= 0)
            m_settings.deferRules.erase(m_settings.deferRules.begin() + removeAt);

        ImGui::SetNextItemWidth(240.f);
        const bool entered = ImGui::InputText("##newrule", m_ruleBuf, sizeof(m_ruleBuf),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("Add rule") || entered) && m_ruleBuf[0] != '\0') {
            m_settings.deferRules.push_back(m_ruleBuf);
            m_ruleBuf[0] = '\0';
        }
        if (m_settings.deferRules.empty())
            ImGui::TextDisabled("No rules yet. Example: add 'omen' or 'Deathrattle'.");

        ImGui::SeparatorText("Value defer (poe2scout)");
        ImGui::TextWrapped("Also defer any revealed item whose live poe2scout price "
                           "(currency, omens, uniques) is at least this many exalted. "
                           "0 = off.");
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", m_settings.minValueExalted);
            ImGui::SetNextItemWidth(110.f);
            if (ImGui::InputText("Min value (exalted)", buf, sizeof(buf),
                                 ImGuiInputTextFlags_CharsDecimal)) {
                const int nv = buf[0] ? std::atoi(buf) : 0;
                m_settings.minValueExalted =
                    nv < 0 ? 0 : (nv > RitualHelperConfig::kMinValueMax
                                      ? RitualHelperConfig::kMinValueMax : nv);
            }
        }
        {
            std::lock_guard<std::mutex> lk(m_fetchMutex);
            if (m_settings.minValueExalted > 0 && m_prices.divinePrice > 0.0) {
                ImGui::SameLine();
                ImGui::TextDisabled("= %.2f divine",
                                    m_settings.minValueExalted / m_prices.divinePrice);
            }
            ImGui::TextDisabled("Prices: %s", m_fetchStatus.c_str());
        }
        if (ImGui::Button(m_fetching ? "Refreshing..." : "Refresh prices") && !m_fetching)
            StartFetch();

        ImGui::SeparatorText("Status");
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

        ImGui::SeparatorText("Debug");
        ImGui::Checkbox("Debug mode", &m_settings.debugMode);
        if (m_settings.debugMode) {
            ImGui::TextWrapped(
                "With the Favours window open, click the button: it writes "
                "debug/ritual-dump.txt with every on-screen inventory (names prove "
                "how the host exposes the ritual window) and every ritual-keyword "
                "UI element (how to find the defer/reroll buttons).");
            if (ImGui::Button("Write ritual dump")) WriteDump();
            if (!m_lastDumpPath.empty())
                ImGui::TextDisabled("Last dump: %s", m_lastDumpPath.c_str());
        }
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
    char m_ruleBuf[96] = {};

    std::thread m_fetchThread;
    std::atomic<bool> m_fetching{false};
    std::atomic<bool> m_fetchAbort{false};
    std::mutex m_fetchMutex;
    RitualHelper::PriceResult m_prices;
    std::string m_fetchStatus = "not fetched yet";

    void StartFetch() {
        if (m_fetching.exchange(true)) return;
        if (m_fetchThread.joinable()) m_fetchThread.join();
        m_fetchAbort = false;
        {
            std::lock_guard<std::mutex> lk(m_fetchMutex);
            m_fetchStatus = "fetching...";
        }
        m_fetchThread = std::thread([this] {
            RitualHelper::PriceResult r = RitualHelper::Poe2Scout::FetchAll(&m_fetchAbort);
            {
                std::lock_guard<std::mutex> lk(m_fetchMutex);
                m_fetchStatus = r.status;
                if (r.ok) m_prices = std::move(r);
            }
            m_fetching = false;
        });
    }

    void FormatValue(char* out, size_t n, double valueEx) {
        double divPrice = 0.0;
        {
            std::lock_guard<std::mutex> lk(m_fetchMutex);
            divPrice = m_prices.divinePrice;
        }
        if (divPrice > 0.0 && valueEx >= divPrice * 0.95)
            std::snprintf(out, n, "%.1f div", valueEx / divPrice);
        else if (valueEx >= 10.0)
            std::snprintf(out, n, "%.0f ex", valueEx);
        else
            std::snprintf(out, n, "%.2f ex", valueEx);
    }

    void FrameTick() {
        if (!m_defer.IsRunning()) return;
        const auto now = Clock::now();
        if (m_window && now - m_lastBottomPoll > std::chrono::milliseconds(100)) {
            m_lastBottomPoll = now;
            const auto texts = RitualHelper::CollectUiTexts(ctx());
            m_bottom = RitualHelper::FindBottomButton(texts, *m_window);
        }
        m_defer.Tick(m_bottom, ctx()->Game.IsForeground());
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
                m_matches = RitualHelper::MatchDeferItems(
                    *m_window, m_settings.deferRules, m_prices.priceExalted,
                    m_settings.minValueExalted);
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
