#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <atomic>

class PowerMonitor {
public:
    using Callback = std::function<void(const std::string&)>;

    PowerMonitor();
    ~PowerMonitor();

    bool Start(Callback cb);
    void Stop();
    void Run();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void OnPowerEvent(const std::string& msg);

    HWND hwnd_ = nullptr;
    Callback callback_;
    std::atomic<bool> running_{ false };
    HPOWERNOTIFY notifications_[16] = {};
    size_t notify_count_ = 0;
};
