// Define INITGUID in exactly one translation unit so that
// windows.h/winnt.h emits actual GUID definitions instead of extern declarations.
#define INITGUID

#include "monitor.h"
#include <powrprof.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>

static std::string NowStr() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::tm tm;
    localtime_s(&tm, &t);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

static const char* GuidName(const GUID* guid) {
    if (IsEqualGUID(*guid, GUID_MONITOR_POWER_ON)) return "Monitor power";
    if (IsEqualGUID(*guid, GUID_SYSTEM_AWAYMODE)) return "Away mode";
    if (IsEqualGUID(*guid, GUID_CONSOLE_DISPLAY_STATE)) return "Display state";
    if (IsEqualGUID(*guid, GUID_ACDC_POWER_SOURCE)) return "AC/DC source";
    if (IsEqualGUID(*guid, GUID_BATTERY_PERCENTAGE_REMAINING)) return "Battery %";
    if (IsEqualGUID(*guid, GUID_IDLE_BACKGROUND_TASK)) return "Idle background task";
    return "Unknown setting";
}

static const char* DisplayStateName(DWORD state) {
    switch (state) {
        case 0: return "Off";
        case 1: return "On";
        case 2: return "Dimmed";
        default: return "Unknown";
    }
}

static const char* AcdcName(DWORD source) {
    switch (source) {
        case 0: return "AC power";
        case 1: return "DC power (battery)";
        case 2: return "Short-term DC (UPS)";
        default: return "Unknown source";
    }
}

PowerMonitor::PowerMonitor() = default;

PowerMonitor::~PowerMonitor() {
    Stop();
}

bool PowerMonitor::Start(Callback cb) {
    callback_ = std::move(cb);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DiagWakePowerMonitor";
    if (!RegisterClassExW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    }

    hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"diag_wake monitor",
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, this);
    if (!hwnd_) return false;

    const GUID* guids[] = {
        &GUID_MONITOR_POWER_ON,
        &GUID_SYSTEM_AWAYMODE,
        &GUID_CONSOLE_DISPLAY_STATE,
        &GUID_ACDC_POWER_SOURCE,
        &GUID_BATTERY_PERCENTAGE_REMAINING,
    };

    for (auto* g : guids) {
        if (notify_count_ < 16) {
            notifications_[notify_count_] = RegisterPowerSettingNotification(hwnd_, g, DEVICE_NOTIFY_WINDOW_HANDLE);
            if (notifications_[notify_count_]) notify_count_++;
        }
    }

    running_ = true;
    OnPowerEvent("Monitor started. Listening for power events...");
    return true;
}

void PowerMonitor::Stop() {
    running_ = false;
    for (size_t i = 0; i < notify_count_; ++i) {
        if (notifications_[i]) {
            UnregisterPowerSettingNotification(notifications_[i]);
            notifications_[i] = nullptr;
        }
    }
    notify_count_ = 0;
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void PowerMonitor::Run() {
    MSG msg;
    while (running_) {
        BOOL ret = GetMessageW(&msg, nullptr, 0, 0);
        if (ret == 0 || ret == -1) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

LRESULT CALLBACK PowerMonitor::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    auto* self = reinterpret_cast<PowerMonitor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void PowerMonitor::OnPowerEvent(const std::string& msg) {
    if (callback_) {
        std::ostringstream ss;
        ss << "[" << NowStr() << "] " << msg;
        callback_(ss.str());
    }
}

LRESULT PowerMonitor::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_POWERBROADCAST) {
        switch (wParam) {
            case PBT_APMSUSPEND:
                OnPowerEvent("SYSTEM ENTERING SLEEP (suspend)");
                break;
            case PBT_APMRESUMESUSPEND:
                OnPowerEvent("SYSTEM RESUMED FROM SLEEP (manual resume)");
                break;
            case PBT_APMRESUMEAUTOMATIC:
                OnPowerEvent("SYSTEM RESUMED FROM SLEEP (automatic/wake timer)");
                break;
            case PBT_APMRESUMECRITICAL:
                OnPowerEvent("SYSTEM RESUMED FROM SLEEP (critical, e.g. battery depleted)");
                break;
            case PBT_APMPOWERSTATUSCHANGE: {
                SYSTEM_POWER_STATUS ps;
                if (GetSystemPowerStatus(&ps)) {
                    std::ostringstream ss;
                    ss << "Power status changed - AC: " << (ps.ACLineStatus == 1 ? "ON" : ps.ACLineStatus == 0 ? "OFF" : "Unknown")
                       << ", Battery: " << (ps.BatteryLifePercent == 255 ? "Unknown" : std::to_string(ps.BatteryLifePercent) + "%");
                    OnPowerEvent(ss.str());
                } else {
                    OnPowerEvent("Power status changed");
                }
                break;
            }
            case PBT_POWERSETTINGCHANGE: {
                auto* setting = reinterpret_cast<POWERBROADCAST_SETTING*>(lParam);
                if (!setting) break;
                std::ostringstream ss;
                ss << "Setting changed: " << GuidName(&setting->PowerSetting);
                if (IsEqualGUID(setting->PowerSetting, GUID_MONITOR_POWER_ON) && setting->DataLength >= sizeof(DWORD)) {
                    ss << " = " << (reinterpret_cast<const DWORD*>(setting->Data)[0] ? "ON" : "OFF");
                } else if (IsEqualGUID(setting->PowerSetting, GUID_CONSOLE_DISPLAY_STATE) && setting->DataLength >= sizeof(DWORD)) {
                    ss << " = " << DisplayStateName(reinterpret_cast<const DWORD*>(setting->Data)[0]);
                } else if (IsEqualGUID(setting->PowerSetting, GUID_ACDC_POWER_SOURCE) && setting->DataLength >= sizeof(DWORD)) {
                    ss << " = " << AcdcName(reinterpret_cast<const DWORD*>(setting->Data)[0]);
                } else if (IsEqualGUID(setting->PowerSetting, GUID_BATTERY_PERCENTAGE_REMAINING) && setting->DataLength >= sizeof(DWORD)) {
                    ss << " = " << reinterpret_cast<const DWORD*>(setting->Data)[0] << "%";
                }
                OnPowerEvent(ss.str());
                break;
            }
            default:
                break;
        }
        return TRUE;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}
