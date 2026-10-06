#include "core.h"
#include "version.h"
#include <cstring>
#include <exception>
#include <stdexcept>

int wmain(int argc, wchar_t *argv[]) {
    bool json_requested = false;
    try {
        std::vector<std::wstring> args;
        bool options = true;
        for (int i = 1; i < argc; ++i) {
            args.emplace_back(argv[i]);
            if (options && args.back() == L"--json")
                json_requested = true;
            if (args.back() == L"--")
                options = false;
        }
        fle::Options o;
        try {
            o = fle::parse(args);
        } catch (const std::invalid_argument &e) {
            fle::Result r;
            r.exit_code = 2;
            r.error.operation = "CLI";
            r.error.message = std::wstring(e.what(), e.what() + strlen(e.what()));
            if (json_requested) {
                const auto text = fle::json(r);
                fle::write_output(GetStdHandle(STD_OUTPUT_HANDLE), {text.begin(), text.end()});
            } else
                fle::write_output(GetStdHandle(STD_ERROR_HANDLE), r.error.message + L". Use --help.\n");
            return 2;
        }
        if (o.help) {
            const std::string banner = "file-lock-explainer " FLE_VERSION "\n";
            fle::write_output(GetStdHandle(STD_OUTPUT_HANDLE), {banner.begin(), banner.end()});
            fle::write_output(
                GetStdHandle(STD_OUTPUT_HANDLE),
                L"Usage: file-lock-explainer [--json] [--] <path>\n"
                L"       file-lock-explainer --help | --version\n"
                L"Diagnose local file associations through Windows Restart Manager.\n"
                L"Directories, network/device paths and reparse points are refused.\n"
                L"No applications identified does not prove that no open handles exist.\n"
                L"Read-only diagnostic calls only; no forced unlocking or process control.\n"
                L"Exit codes: 0 completed, 2 usage, 3 target, 4 access, 5 Windows API, 6 internal.\n");
            return 0;
        }
        if (o.version) {
            const std::string v = "file-lock-explainer " FLE_VERSION "\n";
            fle::write_output(GetStdHandle(STD_OUTPUT_HANDLE), {v.begin(), v.end()});
            return 0;
        }
        const auto r = fle::inspect(o.path);
        if (o.json) {
            const auto text = fle::json(r);
            fle::write_output(GetStdHandle(STD_OUTPUT_HANDLE), {text.begin(), text.end()});
        } else
            fle::write_output(GetStdHandle(r.exit_code ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE),
                              fle::human(r));
        return r.exit_code;
    } catch (const fle::OutputError &e) {
        // Never append a second JSON object after a possibly partial write.
        try {
            fle::write_output(GetStdHandle(STD_ERROR_HANDLE), L"Output failed (Windows " +
                                                                  std::to_wstring(e.windows_code) + L"): " +
                                                                  fle::error_message(e.windows_code) + L"\n");
        } catch (...) {
            return 6;
        }
        return 6;
    } catch (const std::exception &) {
        try {
            fle::Result r;
            r.exit_code = 6;
            r.error.operation = "Internal";
            r.error.message = L"Unexpected internal failure.";
            if (json_requested) {
                const auto text = fle::json(r);
                fle::write_output(GetStdHandle(STD_OUTPUT_HANDLE), {text.begin(), text.end()});
            } else
                fle::write_output(GetStdHandle(STD_ERROR_HANDLE), r.error.message + L"\n");
        } catch (...) {
            return 6; /* Output itself may be unavailable. */
        }
        return 6;
    }
}
