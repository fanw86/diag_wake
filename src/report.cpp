#include "report.h"
#include "eventlog.h"
#include <windows.h>
#include <iostream>
#include <cstdint>

static void PrintPowerStatus() {
    SYSTEM_POWER_STATUS ps;
    if (!GetSystemPowerStatus(&ps)) {
        std::cout << "  Failed to get power status.\n";
        return;
    }

    std::cout << "  AC Line Status:       ";
    switch (ps.ACLineStatus) {
        case 1: std::cout << "Online (plugged in)\n"; break;
        case 0: std::cout << "Offline (on battery)\n"; break;
        default: std::cout << "Unknown\n"; break;
    }

    std::cout << "  Battery Life:         ";
    if (ps.BatteryLifePercent == 255) {
        std::cout << "Unknown\n";
    } else {
        std::cout << static_cast<int>(ps.BatteryLifePercent) << "%\n";
    }

    std::cout << "  Battery Saver:        ";
    if (ps.SystemStatusFlag == 1) {
        std::cout << "Active\n";
    } else {
        std::cout << "Inactive\n";
    }

    if (ps.BatteryLifeTime != (DWORD)-1) {
        int hrs = ps.BatteryLifeTime / 3600;
        int mins = (ps.BatteryLifeTime % 3600) / 60;
        std::cout << "  Estimated Time Left:  " << hrs << "h " << mins << "m\n";
    } else {
        std::cout << "  Estimated Time Left:  Unknown\n";
    }

    if (ps.BatteryFullLifeTime != (DWORD)-1) {
        int hrs = ps.BatteryFullLifeTime / 3600;
        int mins = (ps.BatteryFullLifeTime % 3600) / 60;
        std::cout << "  Full Charge Time:     " << hrs << "h " << mins << "m\n";
    }
}

static void PrintPowerSchemes() {
    std::cout << "\n=== POWER PLANS ===\n";

    FILE* pipe = _popen("powercfg /list", "r");
    if (!pipe) {
        std::cout << "  Failed to query power plans.\n";
        return;
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        // Skip empty lines
        if (buffer[0] == '\n' || buffer[0] == '\r') continue;
        std::cout << "  " << buffer;
    }
    _pclose(pipe);
}

static void PrintRecentEvents() {
    std::cout << "\n=== RECENT POWER EVENTS (last 20) ===\n";
    auto events = ReadRecentPowerEvents(20);
    if (events.empty()) {
        std::cout << "  No power events found in System log.\n";
        return;
    }

    for (const auto& ev : events) {
        std::cout << "  [" << ev.time << "] "
                  << ev.provider << " (ID " << ev.eventId << ")\n"
                  << "    " << ev.description << "\n\n";
    }
}

static const char* EventIdDescription(uint16_t id) {
    switch (id) {
        case 1: return "Power transition initiated";
        case 41: return "System crashed / unclean shutdown (previous sleep may have failed)";
        case 42: return "Entering sleep";
        case 107: return "Resumed from sleep";
        case 131: return "Wake source identified";
        case 132: return "Sleep reason";
        case 506: return "System entering connected standby";
        case 507: return "System exiting connected standby";
        case 5070: return "Sleep/wake diagnostic";
        default: return "";
    }
}

static void PrintEventIdLegend() {
    std::cout << "\n=== EVENT ID LEGEND ===\n";
    uint16_t ids[] = {1, 41, 42, 107, 131, 132, 506, 507};
    for (auto id : ids) {
        std::cout << "  " << id << " - " << EventIdDescription(id) << "\n";
    }
}

void PrintReport() {
    std::cout << "========================================\n";
    std::cout << "  Windows Sleep/Wake Diagnostic Report\n";
    std::cout << "========================================\n";

    std::cout << "\n=== CURRENT POWER STATUS ===\n";
    PrintPowerStatus();

    PrintPowerSchemes();
    PrintRecentEvents();
    PrintEventIdLegend();

    std::cout << "\n========================================\n";
    std::cout << "Report complete.\n";
}
