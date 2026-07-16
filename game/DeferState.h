#pragma once

#include "DeferPlanner.h"
#include "RitualScanner.h"
#include "../input/Win32Input.h"

#include <chrono>
#include <string>
#include <vector>

namespace RitualHelper {

inline constexpr int kSettleMs = 80;
inline constexpr int kPostClickMs = 150;
inline constexpr int kVerifyTimeoutMs = 2500;
inline constexpr int kWatchdogMs = 20000;

class DeferState {
public:
    using Clock = std::chrono::steady_clock;

    bool IsRunning() const { return m_phase != Phase::Idle; }
    const std::string& Status() const { return m_status; }

    void Start(bool alreadyDeferMode, const ScreenRect& toggle,
               std::vector<ScreenRect> items) {
        if (IsRunning()) return;
        m_toggle = toggle;
        m_items = std::move(items);
        m_index = 0;
        m_sub = Sub::Move;
        m_haveCursor = RitualHelperInput::GetCursorScreen(m_savedX, m_savedY);
        m_watchdogAt = Clock::now() + std::chrono::milliseconds(kWatchdogMs);
        m_status = "deferring...";
        m_phase = alreadyDeferMode ? Phase::Items : Phase::Toggle;
    }

    void Tick(const BottomButton& bottom, bool foreground) {
        if (m_phase == Phase::Idle) return;
        const auto now = Clock::now();

        if (!foreground) { Finish("aborted: game lost focus"); return; }
        if (RitualHelperInput::IsRightMouseDown()) { Finish("cancelled"); return; }
        if (now > m_watchdogAt) { Finish("aborted: watchdog"); return; }

        switch (m_phase) {
            case Phase::Toggle:
                if (StepClick(now, Center(m_toggle))) {
                    m_phase = Phase::Verify;
                    m_verifyAt = now + std::chrono::milliseconds(kVerifyTimeoutMs);
                }
                break;
            case Phase::Verify:
                if (bottom.mode == BottomButtonMode::DeferItem) {
                    m_phase = Phase::Items;
                    m_sub = Sub::Move;
                } else if (now > m_verifyAt) {
                    Finish("aborted: defer mode not confirmed");
                }
                break;
            case Phase::Items:
                if (m_index >= m_items.size()) {
                    m_phase = Phase::Apply;
                    m_sub = Sub::Move;
                    break;
                }
                if (StepClick(now, Center(m_items[m_index]))) {
                    ++m_index;
                    m_sub = Sub::Move;
                }
                break;
            case Phase::Apply: {
                if (bottom.mode != BottomButtonMode::DeferItem) {
                    Finish("aborted: defer button vanished");
                    break;
                }
                const ScreenRect r{bottom.x, bottom.y, bottom.w, bottom.h};
                if (StepClick(now, Center(r)))
                    Finish("deferred");
                break;
            }
            default:
                break;
        }
    }

private:
    enum class Phase { Idle, Toggle, Verify, Items, Apply };
    enum class Sub { Move, Settle, Post };

    struct Point { int x = 0; int y = 0; };

    static Point Center(const ScreenRect& r) {
        return Point{static_cast<int>(r.x + r.w * 0.5f + 0.5f),
                     static_cast<int>(r.y + r.h * 0.5f + 0.5f)};
    }

    bool StepClick(Clock::time_point now, Point p) {
        switch (m_sub) {
            case Sub::Move:
                RitualHelperInput::MoveCursorScreen(p.x, p.y);
                m_deadline = now + std::chrono::milliseconds(kSettleMs);
                m_sub = Sub::Settle;
                return false;
            case Sub::Settle:
                if (now < m_deadline) return false;
                RitualHelperInput::LeftClickAtCursor();
                m_deadline = now + std::chrono::milliseconds(kPostClickMs);
                m_sub = Sub::Post;
                return false;
            case Sub::Post:
                if (now < m_deadline) return false;
                m_sub = Sub::Move;
                return true;
        }
        return false;
    }

    void Finish(const char* status) {
        m_phase = Phase::Idle;
        m_status = status;
        if (m_haveCursor)
            RitualHelperInput::MoveCursorScreen(m_savedX, m_savedY);
    }

    Phase m_phase = Phase::Idle;
    Sub m_sub = Sub::Move;
    ScreenRect m_toggle;
    std::vector<ScreenRect> m_items;
    size_t m_index = 0;
    Clock::time_point m_deadline{};
    Clock::time_point m_verifyAt{};
    Clock::time_point m_watchdogAt{};
    int m_savedX = 0, m_savedY = 0;
    bool m_haveCursor = false;
    std::string m_status;
};

}
