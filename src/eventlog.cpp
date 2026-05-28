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

static std::string XmlTimeToString(const std::string& xml) {
    auto pos = xml.find("SystemTime=\"");
    if (pos == std::string::npos) {
        pos = xml.find("SystemTime='");
        if (pos == std::string::npos) return {};
        pos += 12;
        auto end = xml.find('\'', pos);
        if (end == std::string::npos) return {};
        std::string iso = xml.substr(pos, end - pos);
        if (iso.size() >= 19) {
            iso[10] = ' ';
            return iso.substr(0, 19);
        }
        return iso;
    }
    pos += 12;
    auto end = xml.find('"', pos);
    if (end == std::string::npos) return {};
    std::string iso = xml.substr(pos, end - pos);
    // Convert "2023-01-01T12:00:00.0000000Z" to "2023-01-01 12:00:00"
    if (iso.size() >= 19) {
        iso[10] = ' ';
        return iso.substr(0, 19);
    }
    return iso;
}

static std::string XmlExtractTag(const std::string& xml, const std::string& tag) {
    auto start = xml.find("<" + tag + ">");
    if (start == std::string::npos) {
        // Try self-closing or attribute form: <Provider Name="..." or Name='...'
        start = xml.find("<" + tag + " ");
        if (start == std::string::npos) return {};
        start = xml.find("Name=\"", start);
        if (start != std::string::npos) {
            start += 6;
            auto end = xml.find('"', start);
            if (end != std::string::npos) return xml.substr(start, end - start);
        }
        start = xml.find("Name='", start);
        if (start != std::string::npos) {
            start += 6;
            auto end = xml.find('\'', start);
            if (end != std::string::npos) return xml.substr(start, end - start);
        }
        return {};
    }
    start += tag.size() + 2;
    auto end = xml.find("</" + tag + ">", start);
    if (end == std::string::npos) return {};
    return xml.substr(start, end - start);
}

static bool ExtractFromXml(EVT_HANDLE hEvent, PowerEvent& ev) {
    DWORD bufSize = 0;
    DWORD propCount = 0;
    EvtRender(nullptr, hEvent, EvtRenderEventXml, 0, nullptr, &bufSize, &propCount);
    if (bufSize == 0) return false;
    std::vector<wchar_t> buf(bufSize / sizeof(wchar_t) + 1);
    if (!EvtRender(nullptr, hEvent, EvtRenderEventXml, bufSize, buf.data(), &bufSize, &propCount))
        return false;
    std::string xml = WstrToUtf8(buf.data());
    if (xml.empty()) return false;

    ev.provider = XmlExtractTag(xml, "Provider");
    ev.time = XmlTimeToString(xml);
    std::string idStr = XmlExtractTag(xml, "EventID");
    if (!idStr.empty()) {
        try { ev.eventId = static_cast<uint16_t>(std::stoul(idStr)); } catch (...) {}
    }
    std::string lvlStr = XmlExtractTag(xml, "Level");
    if (!lvlStr.empty()) {
        try { ev.level = static_cast<uint8_t>(std::stoul(lvlStr)); } catch (...) {}
    }
    return true;
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

    // EvtNext reads oldest-first by default. Seek backward from the end so we
    // collect the *most recent* events instead of the oldest 50.
    if (!EvtSeek(hQuery, -static_cast<LONGLONG>(maxCount), nullptr, 0, EvtSeekRelativeToLast)) {
        // Fewer than maxCount events total — just start from the beginning.
        EvtSeek(hQuery, 0, nullptr, 0, EvtSeekRelativeToFirst);
    }

    // Create a render context that explicitly requests the system properties we need.
    // Use ULONG_PTR so each element is pointer-sized; on x64 the API reads 8-byte
    // values from this array, so a DWORD[] would misalign the property IDs.
    ULONG_PTR props[] = {
        EvtSystemProviderName,
        EvtSystemTimeCreated,
        EvtSystemEventID,
        EvtSystemLevel
    };
    EVT_HANDLE hContext = EvtCreateRenderContext(
        static_cast<DWORD>(sizeof(props) / sizeof(props[0])),
        reinterpret_cast<LPCWSTR*>(props), EvtRenderContextSystem);

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
                        if (propCount > 1) {
                            if (values[1].Type == EvtVarTypeFileTime) {
                                ULARGE_INTEGER uli;
                                uli.QuadPart = values[1].FileTimeVal;
                                FILETIME ft;
                                ft.dwLowDateTime = uli.LowPart;
                                ft.dwHighDateTime = uli.HighPart;
                                ev.time = FileTimeToString(ft);
                            } else if (values[1].Type == EvtVarTypeSysTime && values[1].SysTimeVal) {
                                SYSTEMTIME st = *values[1].SysTimeVal;
                                std::ostringstream ss;
                                ss << std::setfill('0')
                                   << st.wYear << '-' << std::setw(2) << st.wMonth << '-' << std::setw(2) << st.wDay
                                   << ' ' << std::setw(2) << st.wHour << ':' << std::setw(2) << st.wMinute << ':' << std::setw(2) << st.wSecond;
                                ev.time = ss.str();
                            }
                        }
                        if (propCount > 2) {
                            if (values[2].Type == EvtVarTypeUInt16) {
                                ev.eventId = values[2].UInt16Val;
                            } else if (values[2].Type == EvtVarTypeUInt32) {
                                ev.eventId = static_cast<uint16_t>(values[2].UInt32Val & 0xFFFF);
                            } else if (values[2].Type == EvtVarTypeInt32) {
                                ev.eventId = static_cast<uint16_t>(values[2].Int32Val & 0xFFFF);
                            }
                        }
                        if (propCount > 3 && values[3].Type == EvtVarTypeByte) {
                            ev.level = values[3].ByteVal;
                        }
                    }
                }
            }
            // Fallback: if system-property rendering failed, extract from raw XML.
            if (ev.eventId == 0 || ev.time.empty()) {
                ExtractFromXml(hEvents[i], ev);
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
