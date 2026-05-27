# diag_wake

Windows 10 Sleep/Wake Events Diagnostic Tool

A lightweight C++ console utility that monitors and diagnoses Windows power management events in real-time, reads the System event log for sleep/wake history, and generates diagnostic reports.

## Features

- **Real-time monitoring** (`monitor`) — listens for sleep, wake, display state changes, AC/battery transitions, and power setting changes via the Windows power notification APIs.
- **Event log reader** (`events`) — queries the Windows Event Log for `Microsoft-Windows-Kernel-Power` and `Microsoft-Windows-Power-Troubleshooter` entries with descriptions.
- **Diagnostic report** (`report`) — prints current power status, active power plan, recent events, and an event ID legend.

## Building

### Option A: Visual Studio Developer Command Prompt (no CMake needed)

Open **Developer Command Prompt for VS** (or "x64 Native Tools Command Prompt") and run:

```cmd
cd /d F:\diag_wake
cl /EHsc /W4 /O2 /std:c++17 src\main.cpp src\monitor.cpp src\eventlog.cpp src\report.cpp /Fediag_wake.exe powrprof.lib wevtapi.lib ole32.lib oleaut32.lib
```

### Option B: CMake + Visual Studio

```cmd
cd /d F:\diag_wake
cmake -B build -S . -G "Visual Studio 17 2022"
cmake --build build --config Release
```

The executable will be at `build\Release\diag_wake.exe` (or `build\diag_wake.exe` for single-config generators).

## Usage

```cmd
diag_wake.exe monitor    # Real-time power event monitoring. Ctrl+C to stop.
diag_wake.exe report     # One-time diagnostic report.
diag_wake.exe events     # Show recent power events from System log.
```

Run as Administrator if you want the fullest event log access.

## Event IDs Tracked

| ID  | Meaning                                      |
|-----|----------------------------------------------|
| 1   | Power transition initiated                   |
| 41  | Unclean shutdown / previous sleep failed     |
| 42  | Entering sleep                               |
| 107 | Resumed from sleep                           |
| 131 | Wake source identified                       |
| 132 | Sleep reason                                 |
| 506 | Entering connected standby                   |
| 507 | Exiting connected standby                    |
