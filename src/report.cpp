#include "report.h"
#include "eventlog.h"
#include <windows.h>
#include <powrprof.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>

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

    DWORD schemeGuidSize = 0;
    DWORD schemeCount = 0;
    if (PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, 0, nullptr, &schemeGuidSize, &schemeCount) != ERROR_SUCCESS) {
        std::cout << "  Failed to enumerate power schemes.\n";
        return;
    }

    std::vector<BYTE> schemeBuffer(schemeGuidSize);
    if (PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, 0, schemeBuffer.data(), &schemeGuidSize, &schemeCount) != ERROR_SUCCESS) {
        std::cout << "  Failed to enumerate power schemes.\n";
        return;
    }

    GUID* activeGuid = nullptr;
    DWORD activeSize = sizeof(GUID*);
    PowerGetActiveScheme(nullptr, &activeGuid);

    GUID** schemes = reinterpret_cast<GUID**>(schemeBuffer.data());
    for (DWORD i = 0; i < schemeCount; ++i) {
        DWORD nameSize = 0;
        PowerReadFriendlyName(nullptr, schemes[i], nullptr, nullptr, nullptr, &nameSize);
        if (nameSize > 0) {
            std::vector<BYTE> nameBuf(nameSize);
            if (PowerReadFriendlyName(nullptr, schemes[i], nullptr, nullptr, nameBuf.data(), &nameSize) == ERROR_SUCCESS) {
                std::wstring name(reinterpret_cast<wchar_t*>(nameBuf.data()), nameBuf.size() / sizeof(wchar_t));
                std::wcout << L"  ";
                if (activeGuid && IsEqualGUID(*activeGuid, *schemes[i])) {
                    std::wcout << L"[ACTIVE] ";
                } else {
                    std::wcout << L"         ";
                }
                std::wcout << name;

                // Print GUID
                wchar_t guidStr[40] = {};
                StringFromGUID2(*schemes[i], guidStr, 39);
                std::wcout << L" (" << guidStr << L")\n";
            }
        }
    }

    if (activeGuid) LocalFree(activeGuid);
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
