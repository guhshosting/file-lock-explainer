#include "core.h"
#include "version.h"
#include <iomanip>
#include <sstream>

namespace fle {
std::string app_type(RM_APP_TYPE type) {
    switch (type) {
    case RmUnknownApp:
        return "unknown";
    case RmMainWindow:
        return "main_window";
    case RmOtherWindow:
        return "other_window";
    case RmService:
        return "service";
    case RmExplorer:
        return "explorer";
    case RmConsole:
        return "console";
    case RmCritical:
        return "critical";
    default:
        return "unknown";
    }
}
std::vector<std::string> status_names(ULONG status) {
    std::vector<std::string> names;
    if (status == RmStatusUnknown)
        names.push_back("unknown");
    const std::pair<ULONG, const char *> flags[] = {{RmStatusRunning, "running"},
                                                    {RmStatusStopped, "stopped"},
                                                    {RmStatusStoppedOther, "stopped_other"},
                                                    {RmStatusRestarted, "restarted"},
                                                    {RmStatusErrorOnStop, "error_on_stop"},
                                                    {RmStatusErrorOnRestart, "error_on_restart"},
                                                    {RmStatusShutdownMasked, "shutdown_masked"},
                                                    {RmStatusRestartMasked, "restart_masked"}};
    ULONG known = 0;
    for (const auto &flag : flags) {
        known |= flag.first;
        if (status & flag.first)
            names.emplace_back(flag.second);
    }
    if (status & ~known)
        names.push_back("unknown_bits");
    return names;
}
static std::string quote(const std::wstring &text) {
    // Escape UTF-16 units individually: this preserves even unpaired surrogates in JSON.
    std::ostringstream out;
    out << '"';
    for (wchar_t wc : text) {
        const auto c = static_cast<unsigned short>(wc);
        if (c == '"')
            out << "\\\"";
        else if (c == '\\')
            out << "\\\\";
        else if (c >= 32 && c < 127)
            out << static_cast<char>(c);
        else
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << c << std::dec;
    }
    out << '"';
    return out.str();
}
static std::wstring widen(const std::string &text) { return {text.begin(), text.end()}; }
static std::wstring type_label(RM_APP_TYPE type) {
    switch (type) {
    case RmUnknownApp:
        return L"Unknown";
    case RmMainWindow:
        return L"Main window";
    case RmOtherWindow:
        return L"Other window";
    case RmService:
        return L"Windows service";
    case RmExplorer:
        return L"Explorer";
    case RmConsole:
        return L"Console";
    case RmCritical:
        return L"Critical system process";
    default:
        return L"Unknown (" + std::to_wstring(static_cast<int>(type)) + L")";
    }
}
static std::string strings(const std::vector<std::string> &names) {
    std::string out = "[";
    for (const auto &name : names) {
        if (out.size() > 1)
            out += ',';
        out += quote(widen(name));
    }
    return out + "]";
}
static std::string time_hex(const FILETIME &time) {
    std::ostringstream out;
    out << std::hex << std::setw(8) << std::setfill('0') << time.dwHighDateTime << std::setw(8)
        << time.dwLowDateTime;
    return out.str();
}
std::string json(const Result &r) {
    std::ostringstream out;
    out << "{\"schema_version\":1,\"application_version\":\"" FLE_VERSION "\",\"requested_path\":"
        << quote(r.requested) << ",\"inspected_path\":" << (r.inspected.empty() ? "null" : quote(r.inspected))
        << ",\"resource_type\":\"" << r.type << "\",\"result_state\":\"" << r.state
        << "\",\"exit_code\":" << r.exit_code << ",\"associated_application_count\":" << r.apps.size()
        << ",\"reboot_reason_flags\":" << r.reboot_reasons << ",\"applications\":[";
    bool first = true;
    for (const auto &a : r.apps) {
        if (!first)
            out << ',';
        first = false;
        out << "{\"pid\":" << a.Process.dwProcessId << ",\"name\":" << quote(a.strAppName)
            << ",\"application_type\":\"" << app_type(a.ApplicationType)
            << "\",\"application_type_value\":" << static_cast<int>(a.ApplicationType)
            << ",\"restartable\":" << (a.bRestartable ? "true" : "false")
            << ",\"status_flags\":" << a.AppStatus
            << ",\"status_names\":" << strings(status_names(a.AppStatus))
            << ",\"service_name\":" << (a.strServiceShortName[0] ? quote(a.strServiceShortName) : "null")
            << ",\"process_start_time_filetime_hex\":\"" << time_hex(a.Process.ProcessStartTime)
            << "\",\"terminal_session_id\":" << a.TSSessionId << "}";
    }
    out << "],\"warnings\":[";
    first = true;
    for (const auto &w : r.warnings) {
        if (!first)
            out << ',';
        first = false;
        out << quote(w);
    }
    out << "],\"error\":";
    if (r.error.operation.empty())
        out << "null";
    else {
        out << "{\"operation\":" << quote(widen(r.error.operation)) << ",\"windows_code\":" << r.error.code
            << ",\"windows_code_hex\":\"0x" << std::hex << std::setw(8) << std::setfill('0') << r.error.code
            << std::dec << "\",\"message\":" << quote(r.error.message) << "}";
    }
    out << "}\n";
    return out.str();
}
std::wstring human(const Result &r) {
    std::wostringstream out;
    out << L"Resource: " << (r.inspected.empty() ? r.requested : r.inspected) << L'\n';
    if (!r.error.operation.empty()) {
        out << widen(r.error.operation) << L": " << r.error.message << L" (Windows " << r.error.code
            << L", 0x" << std::hex << std::setw(8) << std::setfill(L'0') << r.error.code << std::dec
            << L")\n";
    } else if (r.apps.empty())
        out << L"No applications or services were identified for this resource through Windows Restart "
               L"Manager.\n";
    else {
        out << L"Associated applications/services: " << r.apps.size() << L'\n';
        std::size_t index = 0;
        for (const auto &a : r.apps) {
            out << L"\n"
                << ++index << L". " << (a.strAppName[0] ? a.strAppName : L"(unnamed)") << L"\n   PID: "
                << a.Process.dwProcessId << L"\n   Type: " << type_label(a.ApplicationType)
                << L"\n   Restartable (Restart Manager): " << (a.bRestartable ? L"Yes" : L"No")
                << L"\n   Status: ";
            bool first = true;
            for (const auto &n : status_names(a.AppStatus)) {
                if (!first)
                    out << L", ";
                first = false;
                out << widen(n);
            }
            if (a.AppStatus & ~255UL)
                out << L" (raw 0x" << std::hex << a.AppStatus << std::dec << L")";
            if (a.strServiceShortName[0])
                out << L"\n   Service: " << a.strServiceShortName;
            out << L"\n   Process start identity (FILETIME hex): "
                << widen(time_hex(a.Process.ProcessStartTime)) << L'\n';
        }
    }
    for (const auto &w : r.warnings)
        out << L"Note: " << w << L'\n';
    return out.str();
}
} // namespace fle
