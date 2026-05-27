#include <windows.h>
#include <iostream>
#include <string>
#include "monitor.h"
#include "report.h"
#include "eventlog.h"

static void PrintUsage(const char* prog) {
    std::cout << "Windows Sleep/Wake Diagnostics Tool v1.0\n\n";
    std::cout << "Usage: " << prog << " <command>\n\n";
    std::cout << "Commands:\n";
    std::cout << "  monitor   Monitor power events in real-time. Press Ctrl+C to stop.\n";
    std::cout << "  report    Generate a one-time diagnostic report.\n";
    std::cout << "  events    Show recent power events from the System event log.\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << prog << " monitor\n";
    std::cout << "  " << prog << " report\n";
    std::cout << "  " << prog << " events\n";
}

static void RunMonitor() {
    std::cout << "Starting real-time power event monitor.\n";
    std::cout << "This will show sleep, wake, display, and battery events as they occur.\n";
    std::cout << "Press Ctrl+C to stop.\n\n";

    PowerMonitor mon;
    if (!mon.Start([](const std::string& msg) {
        std::cout << msg << "\n";
    })) {
        std::cerr << "Failed to start power monitor.\n";
        return;
    }

    // Set console control handler to gracefully stop on Ctrl+C
    static PowerMonitor* gMon = &mon;
    SetConsoleCtrlHandler([](DWORD evt) -> BOOL {
        if (evt == CTRL_C_EVENT || evt == CTRL_BREAK_EVENT) {
            if (gMon) gMon->Stop();
            return TRUE;
        }
        return FALSE;
    }, TRUE);

    mon.Run();
    std::cout << "\nMonitor stopped.\n";
}

static void RunEvents() {
    std::cout << "Fetching recent power events from System event log...\n\n";
    auto events = ReadRecentPowerEvents(50);
    if (events.empty()) {
        std::cout << "No power events found. Try running as Administrator.\n";
        return;
    }

    for (const auto& ev : events) {
        std::cout << "[" << ev.time << "] "
                  << ev.provider << " | Event ID " << ev.eventId;
        if (ev.level == 1) std::cout << " (Critical)";
        else if (ev.level == 2) std::cout << " (Error)";
        else if (ev.level == 3) std::cout << " (Warning)";
        else if (ev.level == 4) std::cout << " (Info)";
        std::cout << "\n";
        if (!ev.description.empty() && ev.description != "(no description available)") {
            // Print description, but truncate if extremely long
            if (ev.description.size() > 300) {
                std::cout << "  " << ev.description.substr(0, 300) << "...\n";
            } else {
                std::cout << "  " << ev.description << "\n";
            }
        }
        std::cout << "\n";
    }
    std::cout << events.size() << " event(s) shown.\n";
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);

    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }

    std::string cmd = argv[1];
    if (cmd == "monitor" || cmd == "m") {
        RunMonitor();
    } else if (cmd == "report" || cmd == "r") {
        PrintReport();
    } else if (cmd == "events" || cmd == "e") {
        RunEvents();
    } else {
        std::cerr << "Unknown command: " << cmd << "\n\n";
        PrintUsage(argv[0]);
        return 1;
    }

    return 0;
}
