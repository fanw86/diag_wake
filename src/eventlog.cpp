#include "eventlog.h"
#include <windows.h>
#include <winevt.h>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "wevtapi.lib")

static std::string WstrToUtf8(const wchar_t* wstr) {
    if (!wstr) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out.data(), len, nullptr, nullptr);
    return out;
}

static std::string FileTimeToString(const FILETIME& ft) {
    FILETIME localFt;
    FileTimeToLocalFileTime(&ft, &localFt);
    SYSTEMTIME st;
    FileTimeToSystemTime(&localFt, &st);
    std::ostringstream ss;
    ss << std::setfill('0')
       << st.wYear << '-' << std::setw(2) << st.wMonth << '-' << std::setw(2) << st.wDay
       << ' ' << std::setw(2) << st.wHour << ':' << std::setw(2) << st.wMinute << ':' << std::setw(2) << st.wSecond;
    return ss.str();
}

std::vector<PowerEvent> ReadRecentPowerEvents(int maxCount) {
    std::vector<PowerEvent> results;

    const wchar_t* xpath =
        L"<QueryList>"
        L"<Query Id=\"0\" Path=\"System\">"
        L"<Select Path=\"System\">"
        L"*[System[Provider[@Name='Microsoft-Windows-Kernel-Power' or @Name='Microsoft-Windows-Power-Troubleshooter']]]"
        L"</Select>"
        L"</Query>"
        L"</QueryList>";

    EVT_HANDLE hQuery = EvtQuery(nullptr, nullptr, xpath, EvtQueryChannelPath);
    if (!hQuery) {
        return results;
    }

    // Create a render context that explicitly requests the system properties we need.
    EVT_SYSTEM_PROPERTY_ID props[] = {
        EvtSystemProviderName,
        EvtSystemTimeCreated,
        EvtSystemEventID,
        EvtSystemLevel
    };
    EVT_HANDLE hContext = EvtCreateRenderContext(
        static_cast<DWORD>(std::size(props)), props, EvtRenderContextSystem);

    EVT_HANDLE hEvents[16];
    DWORD count = 0;
    while (results.size() < static_cast<size_t>(maxCount) &&
           EvtNext(hQuery, 16, hEvents, INFINITE, 0, &count)) {
        for (DWORD i = 0; i < count && results.size() < static_cast<size_t>(maxCount); ++i) {
            PowerEvent ev;

            if (hContext) {
                DWORD bufSize = 0;
                DWORD propCount = 0;
                EvtRender(hContext, hEvents[i], EvtRenderEventValues, 0, nullptr, &bufSize, &propCount);
                if (bufSize > 0) {
                    std::vector<BYTE> buffer(bufSize);
                    EVT_VARIANT* values = reinterpret_cast<EVT_VARIANT*>(buffer.data());
                    if (EvtRender(hContext, hEvents[i], EvtRenderEventValues, bufSize, values, &bufSize, &propCount)) {
                        // 0 = ProviderName, 1 = TimeCreated, 2 = EventID, 3 = Level
                        if (propCount > 0 && values[0].Type == EvtVarTypeString) {
                            ev.provider = WstrToUtf8(values[0].StringVal);
                        }
                        if (propCount > 1 && values[1].Type == EvtVarTypeFileTime) {
                            ULARGE_INTEGER uli;
                            uli.QuadPart = values[1].FileTimeVal;
                            FILETIME ft;
                            ft.dwLowDateTime = uli.LowPart;
                            ft.dwHighDateTime = uli.HighPart;
                            ev.time = FileTimeToString(ft);
                        }
                        if (propCount > 2) {
                            if (values[2].Type == EvtVarTypeUInt16) {
                                ev.eventId = values[2].UInt16Val;
                            } else if (values[2].Type == EvtVarTypeUInt32) {
                                ev.eventId = static_cast<uint16_t>(values[2].UInt32Val & 0xFFFF);
                            }
                        }
                        if (propCount > 3 && values[3].Type == EvtVarTypeByte) {
                            ev.level = values[3].ByteVal;
                        }
                    }
                }
            }

            // Try to get a description via FormatMessage.
            EVT_HANDLE hPub = nullptr;
            if (ev.provider.find("Troubleshooter") != std::string::npos) {
                hPub = EvtOpenPublisherMetadata(nullptr, L"Microsoft-Windows-Power-Troubleshooter", nullptr, 0, 0);
            }
            if (!hPub) {
                hPub = EvtOpenPublisherMetadata(nullptr, L"Microsoft-Windows-Kernel-Power", nullptr, 0, 0);
            }
            if (hPub) {
                DWORD size = 0;
                DWORD r = EvtFormatMessage(hPub, hEvents[i], 0, 0, nullptr, EvtFormatMessageEvent, 0, nullptr, &size);
                if ((r == ERROR_INSUFFICIENT_BUFFER || r == ERROR_SUCCESS) && size > 0) {
                    std::vector<wchar_t> msgBuf(size);
                    if (EvtFormatMessage(hPub, hEvents[i], 0, 0, nullptr, EvtFormatMessageEvent, size, msgBuf.data(), &size)) {
                        ev.description = WstrToUtf8(msgBuf.data());
                    }
                }
                EvtClose(hPub);
            }
            if (ev.description.empty()) {
                ev.description = "(no description available)";
            }

            EvtClose(hEvents[i]);
            results.push_back(std::move(ev));
        }
    }

    for (DWORD i = 0; i < count; ++i) {
        if (hEvents[i]) EvtClose(hEvents[i]);
    }

    if (hContext) EvtClose(hContext);
    EvtClose(hQuery);
    return results;
}
