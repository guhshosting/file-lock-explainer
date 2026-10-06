#include "core.h"
#include <iostream>
#include <stdexcept>
#include <cstdio>

static void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
class Fake final : public fle::RmApi {
  public:
    DWORD start_code = 0, register_code = 0, list_code = 0, end_code = 0;
    unsigned calls = 0, ends = 0;
    bool changing = false, forever = false, shrinking = false, invalid_count = false;
    UINT total = 2;
    DWORD start(DWORD &id) override {
        id = 42;
        return start_code;
    }
    DWORD register_file(DWORD, const wchar_t *) override { return register_code; }
    DWORD end(DWORD) noexcept override {
        ++ends;
        return end_code;
    }
    DWORD list(DWORD, UINT &needed, UINT &count, RM_PROCESS_INFO *apps, DWORD &) override {
        ++calls;
        if (invalid_count) {
            count = 1;
            return ERROR_SUCCESS;
        }
        if (list_code)
            return list_code;
        needed = forever ? count + 1 : ((changing && calls == 2) ? 3 : total);
        if (shrinking)
            needed = calls == 1 ? 3 : 1;
        if (changing && calls == 2)
            total = 3;
        if (count < needed) {
            count = 0;
            return ERROR_MORE_DATA;
        }
        count = needed;
        for (UINT i = 0; i < count; ++i) {
            apps[i] = {};
            apps[i].Process.dwProcessId = i + 100;
        }
        return ERROR_SUCCESS;
    }
};
int main() {
    try {
        check(fle::parse({L"--help"}).help, "help");
        check(fle::parse({L"--version"}).version, "version");
        check(fle::parse({L"--json", L"space 雪.txt"}).json, "json Unicode");
        check(fle::parse({L"--", L"-file"}).path == L"-file", "separator");
        for (const auto &args : std::vector<std::vector<std::wstring>>{{},
                                                                       {L"--bad"},
                                                                       {L"a", L"b"},
                                                                       {L"--json"},
                                                                       {L"--help", L"a"},
                                                                       {L"--json", L"--json", L"a"}}) {
            bool threw = false;
            try {
                (void)fle::parse(args);
            } catch (const std::invalid_argument &) {
                threw = true;
            }
            check(threw, "invalid CLI");
        }
        fle::Result seed;
        seed.inspected = L"C:\\test";
        {
            Fake f;
            f.total = 0;
            auto r = fle::enumerate(seed, f);
            check(r.state == "none_identified" && f.ends == 1, "empty");
        }
        {
            Fake f;
            auto result = fle::enumerate(seed, f);
            check(result.apps.size() == 2 && f.calls == 2 && f.ends == 1,
                  "normal sizing and multiple results");
        }
        {
            Fake f;
            f.shrinking = true;
            auto result = fle::enumerate(seed, f);
            check(result.apps.size() == 1 && f.calls == 2, "shrinking list");
        }
        {
            Fake f;
            f.invalid_count = true;
            auto result = fle::enumerate(seed, f);
            check(result.error.code == ERROR_INVALID_DATA && f.ends == 1, "invalid API count");
        }
        {
            Fake f;
            f.total = 65537;
            auto result = fle::enumerate(seed, f);
            check(result.error.operation == "RmGetList safety limit" && f.ends == 1, "allocation bound");
        }
        {
            Fake f;
            f.end_code = ERROR_INVALID_HANDLE;
            auto result = fle::enumerate(seed, f);
            check(result.error.operation == "RmEndSession" && f.ends == 1, "cleanup failure");
        }
        {
            Fake f;
            f.changing = true;
            auto r = fle::enumerate(seed, f);
            check(r.apps.size() == 3 && f.calls == 3 && f.ends == 1, "changing list");
        }
        {
            Fake f;
            f.forever = true;
            auto r = fle::enumerate(seed, f);
            check(r.state == "limited" && f.calls == 8 && f.ends == 1, "bounded retries");
        }
        {
            Fake f;
            f.start_code = ERROR_ACCESS_DENIED;
            auto r = fle::enumerate(seed, f);
            check(r.exit_code == 4 && f.ends == 0, "session failure");
        }
        {
            Fake f;
            f.register_code = ERROR_BAD_ARGUMENTS;
            auto r = fle::enumerate(seed, f);
            check(r.error.operation == "RmRegisterResources" && f.ends == 1, "register failure");
        }
        {
            Fake f;
            f.list_code = ERROR_INVALID_HANDLE;
            auto r = fle::enumerate(seed, f);
            check(r.error.operation == "RmGetList" && f.ends == 1, "list failure");
        }
        check(!fle::error_message(ERROR_ACCESS_DENIED).empty(), "error format");
        check(fle::error_message(0xffffffff) == L"No system message is available.", "unknown error");
        bool output_failed = false;
        try {
            fle::write_output(INVALID_HANDLE_VALUE, L"test");
        } catch (const fle::OutputError &error) {
            output_failed = error.windows_code == ERROR_INVALID_HANDLE;
        }
        check(output_failed, "specific output failure");
        check(fle::status_names(255) == std::vector<std::string>{"running", "stopped", "stopped_other",
                                                                 "restarted", "error_on_stop",
                                                                 "error_on_restart", "shutdown_masked",
                                                                 "restart_masked"},
              "status flags");
        check(fle::status_names(0) == std::vector<std::string>{"unknown"}, "zero status");
        check(fle::app_type(RmMainWindow) == "main_window" &&
                  fle::app_type(RmOtherWindow) == "other_window" && fle::app_type(RmExplorer) == "explorer" &&
                  fle::app_type(RmConsole) == "console" && fle::app_type(RmCritical) == "critical",
              "SDK application types");
        check(fle::status_names(256) == std::vector<std::string>{"unknown_bits"}, "future status");
        // Deliberate unknown future value within the SDK enum's 0..1023 C++ range.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        check(fle::app_type(static_cast<RM_APP_TYPE>(777)) == "unknown", "future type");
        fle::Result r;
        r.state = "none_identified";
        check(fle::human(r).find(L"No applications") != std::wstring::npos, "empty human");
        RM_PROCESS_INFO a{};
        r.state = "identified";
        r.requested = std::wstring(L"quote\" slash\\ line\n tab\t") + wchar_t(0) + L"\U0001f512";
        a.ApplicationType = RmService;
        a.AppStatus = 255;
        a.bRestartable = TRUE;
        wcscpy_s(a.strAppName, L"service 雪\"\\\n");
        wcscpy_s(a.strServiceShortName, L"Svc");
        r.apps.push_back(a);
        check(fle::human(r).find(L"Service: Svc") != std::wstring::npos, "service human");
        check(fle::json(r) == fle::json(r), "determinism");
        // Emit model fixtures for independent JSON parsing in the integration test.
        std::cout << fle::json(r);
        a.bRestartable = FALSE;
        // Deliberate future SDK value, as above.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        a.ApplicationType = static_cast<RM_APP_TYPE>(777);
        a.AppStatus = 256;
        r.apps.push_back(a);
        std::cout << fle::json(r);
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
