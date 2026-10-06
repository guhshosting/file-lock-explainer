#include "core.h"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

namespace fle {
Options parse(const std::vector<std::wstring> &args) {
    Options o;
    bool options = true;
    for (const auto &a : args) {
        if (options && a == L"--") {
            options = false;
            continue;
        }
        if (options && a == L"--json") {
            if (o.json)
                throw std::invalid_argument("Duplicate --json");
            o.json = true;
        } else if (options && a == L"--help")
            o.help = true;
        else if (options && a == L"--version")
            o.version = true;
        else if (options && !a.empty() && a.front() == L'-')
            throw std::invalid_argument("Unknown option");
        else {
            if (!o.path.empty() || a.empty())
                throw std::invalid_argument("Supply exactly one nonempty path");
            o.path = a;
        }
    }
    if (o.help || o.version) {
        if (o.help == o.version || o.json || !o.path.empty())
            throw std::invalid_argument("Help/version must be used alone");
    } else if (o.path.empty())
        throw std::invalid_argument("A path is required");
    return o;
}
std::wstring error_message(DWORD code) {
    wchar_t *buffer = nullptr;
    const DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                          FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr, code, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    const std::unique_ptr<wchar_t, decltype(&LocalFree)> owner(buffer, &LocalFree);
    std::wstring message;
    if (size && buffer)
        message.assign(buffer, size);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
        message.pop_back();
    return message.empty() ? L"No system message is available." : message;
}
static Result fail(Result r, const char *operation, DWORD code, ExitCode exit_code) {
    r.error = {operation, code, error_message(code)};
    r.exit_code = static_cast<int>(exit_code);
    r.state = "failed";
    return r;
}
class NativeRm final : public RmApi {
    DWORD start(DWORD &s) override {
        wchar_t key[CCH_RM_SESSION_KEY + 1]{};
        return RmStartSession(&s, 0, key);
    }
    DWORD register_file(DWORD s, const wchar_t *p) override {
        return RmRegisterResources(s, 1, &p, 0, nullptr, 0, nullptr);
    }
    DWORD list(DWORD s, UINT &n, UINT &c, RM_PROCESS_INFO *p, DWORD &r) override {
        return RmGetList(s, &n, &c, p, &r);
    }
    DWORD end(DWORD s) noexcept override { return RmEndSession(s); }
};
class Session {
    RmApi &api_;
    DWORD id_;

  public:
    Session(RmApi &api, DWORD id) : api_(api), id_(id) {}
    ~Session() {
        if (active_)
            (void)api_.end(id_);
    }
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    DWORD close() noexcept {
        active_ = false;
        return api_.end(id_);
    }

  private:
    bool active_ = true;
};
class MetadataHandle {
    HANDLE handle_;

  public:
    explicit MetadataHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~MetadataHandle() {
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
    }
    MetadataHandle(const MetadataHandle &) = delete;
    MetadataHandle &operator=(const MetadataHandle &) = delete;
    MetadataHandle(MetadataHandle &&other) noexcept
        : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}
    HANDLE get() const noexcept { return handle_; }
};
Result enumerate(Result r, RmApi &api) {
    DWORD id = 0;
    DWORD code = api.start(id);
    if (code != ERROR_SUCCESS)
        return fail(std::move(r), "RmStartSession", code,
                    code == ERROR_ACCESS_DENIED ? ExitCode::access : ExitCode::inspection);
    Session session(api, id);
    code = api.register_file(id, r.inspected.c_str());
    if (code != ERROR_SUCCESS)
        return fail(std::move(r), "RmRegisterResources", code,
                    code == ERROR_ACCESS_DENIED ? ExitCode::access : ExitCode::inspection);
    std::vector<RM_PROCESS_INFO> buffer;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        UINT needed = 0, count = static_cast<UINT>(buffer.size());
        code = api.list(id, needed, count, buffer.empty() ? nullptr : buffer.data(), r.reboot_reasons);
        if (code == ERROR_SUCCESS) {
            if (count > buffer.size())
                return fail(std::move(r), "RmGetList invalid count", ERROR_INVALID_DATA,
                            ExitCode::inspection);
            buffer.resize(count);
            r.apps = std::move(buffer);
            r.state = r.apps.empty() ? "none_identified" : "identified";
            r.exit_code = 0;
            const DWORD end_code = session.close();
            if (end_code != ERROR_SUCCESS) {
                r.warnings.push_back(L"Restart Manager session cleanup failed.");
                return fail(std::move(r), "RmEndSession", end_code, ExitCode::inspection);
            }
            return r;
        }
        if (code != ERROR_MORE_DATA)
            return fail(std::move(r), "RmGetList", code,
                        code == ERROR_ACCESS_DENIED ? ExitCode::access : ExitCode::inspection);
        if (needed > 65536 || buffer.size() >= 65536)
            return fail(std::move(r), "RmGetList safety limit", ERROR_NOT_ENOUGH_MEMORY,
                        ExitCode::inspection);
        buffer.resize(std::max<std::size_t>(needed, buffer.size() + 1));
    }
    r = fail(std::move(r), "RmGetList retry exhaustion", ERROR_MORE_DATA, ExitCode::inspection);
    r.state = "limited";
    r.warnings.push_back(L"The associated application list kept changing; no partial list is presented.");
    return r;
}
Result inspect(const std::wstring &path) {
    Result r;
    r.requested = path;
    // Reject device/UNC namespaces before any filesystem query. Extended DOS paths are preserved.
    const bool extended = path.rfind(L"\\\\?\\", 0) == 0;
    if ((path.rfind(L"\\\\", 0) == 0 && !extended) ||
        (extended && (path.size() < 7 || path[5] != L':' || path[6] != L'\\')))
        return fail(std::move(r), "Network/device path refused", ERROR_NOT_SUPPORTED, ExitCode::target);
    if (extended)
        r.inspected = path;
    else {
        DWORD size = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
        if (!size)
            return fail(std::move(r), "GetFullPathNameW", GetLastError(), ExitCode::target);
        std::vector<wchar_t> full(static_cast<std::size_t>(size) + 1);
        DWORD used = GetFullPathNameW(path.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
        if (!used || used >= full.size())
            return fail(std::move(r), "GetFullPathNameW", used ? ERROR_RETRY : GetLastError(),
                        ExitCode::target);
        r.inspected.assign(full.data(), used);
        if (r.inspected.rfind(L"\\\\.\\", 0) == 0)
            return fail(std::move(r), "DOS device name refused", ERROR_NOT_SUPPORTED, ExitCode::target);
    }
    const std::size_t offset = extended ? 4 : 0;
    if (r.inspected.size() < offset + 3 || r.inspected[offset + 1] != L':' ||
        r.inspected[offset + 2] != L'\\')
        return fail(std::move(r), "Local drive path required", ERROR_NOT_SUPPORTED, ExitCode::target);
    const std::wstring root = r.inspected.substr(offset, 3);
    if (!extended) {
        // DOS devices are special even inside a directory and with an extension.
        // Refuse them before opening anything; extended DOS paths name literal files.
        for (std::size_t begin = 3; begin < r.inspected.size();) {
            const auto separator = r.inspected.find(L'\\', begin);
            auto component = r.inspected.substr(begin, separator == std::wstring::npos ? std::wstring::npos
                                                                                       : separator - begin);
            const auto suffix = component.find_first_of(L".:");
            if (suffix != std::wstring::npos)
                component.resize(suffix);
            while (!component.empty() && component.back() == L' ')
                component.pop_back();
            std::transform(component.begin(), component.end(), component.begin(), [](wchar_t c) {
                return c >= L'a' && c <= L'z' ? static_cast<wchar_t>(c - L'a' + L'A') : c;
            });
            const bool port = component.size() == 4 &&
                              (component.rfind(L"COM", 0) == 0 || component.rfind(L"LPT", 0) == 0) &&
                              ((component[3] >= L'0' && component[3] <= L'9') || component[3] == L'\u00b9' ||
                               component[3] == L'\u00b2' || component[3] == L'\u00b3');
            if (port || component == L"NUL" || component == L"CON" || component == L"PRN" ||
                component == L"AUX" || component == L"CLOCK$" || component == L"CONIN$" ||
                component == L"CONOUT$")
                return fail(std::move(r), "DOS device name refused", ERROR_NOT_SUPPORTED, ExitCode::target);
            if (separator == std::wstring::npos)
                break;
            begin = separator + 1;
        }
    }
    // Inspect the DOS mapping locally before querying metadata. SUBST and other aliases
    // can hide remote paths; only ordinary local disk/CD/RAM volume mappings are accepted.
    std::vector<wchar_t> mapping(32768);
    const std::wstring drive_name = root.substr(0, 2);
    if (!QueryDosDeviceW(drive_name.c_str(), mapping.data(), static_cast<DWORD>(mapping.size())))
        return fail(std::move(r), "QueryDosDeviceW", GetLastError(), ExitCode::target);
    const std::wstring device = mapping.data();
    if (device.rfind(L"\\Device\\HarddiskVolume", 0) != 0 && device.rfind(L"\\Device\\CdRom", 0) != 0 &&
        device.rfind(L"\\Device\\Ramdisk", 0) != 0)
        return fail(std::move(r), "Drive alias/network mapping refused", ERROR_NOT_SUPPORTED,
                    ExitCode::target);
    const UINT drive = GetDriveTypeW(root.c_str());
    if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_CDROM && drive != DRIVE_RAMDISK)
        return fail(std::move(r), "Nonlocal/unavailable drive refused", ERROR_NOT_SUPPORTED,
                    ExitCode::target);
    // Pin ancestors against rename/delete while descending and while RM inspects.
    // OPEN_REPARSE_POINT avoids following the final component. Never request data/write access.
    std::vector<MetadataHandle> ancestors;
    for (std::size_t end = offset + 3; end <= r.inspected.size();) {
        const std::wstring part = r.inspected.substr(0, end);
        DWORD attrs = INVALID_FILE_ATTRIBUTES;
        {
            MetadataHandle handle(CreateFileW(
                part.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (handle.get() == INVALID_HANDLE_VALUE) {
                const DWORD e = GetLastError();
                return fail(std::move(r), "CreateFileW metadata", e,
                            e == ERROR_ACCESS_DENIED ? ExitCode::access : ExitCode::target);
            }
            FILE_ATTRIBUTE_TAG_INFO info{};
            if (!GetFileInformationByHandleEx(handle.get(), FileAttributeTagInfo, &info, sizeof(info)))
                return fail(std::move(r), "GetFileInformationByHandleEx", GetLastError(), ExitCode::target);
            attrs = info.FileAttributes;
            ancestors.push_back(std::move(handle));
        }
        if (attrs & FILE_ATTRIBUTE_REPARSE_POINT)
            return fail(std::move(r), "Reparse point refused", ERROR_NOT_SUPPORTED, ExitCode::target);
        if (end == r.inspected.size()) {
            r.type = (attrs & FILE_ATTRIBUTE_DIRECTORY) ? "directory" : "file";
            if (r.type == "directory")
                return fail(std::move(r), "Directory inspection unsupported by Restart Manager",
                            ERROR_NOT_SUPPORTED, ExitCode::target);
            break;
        }
        const auto next = r.inspected.find(L'\\', end + 1);
        end = next == std::wstring::npos ? r.inspected.size() : next;
    }
    r.warnings.push_back(L"Restart Manager does not identify every open handle or every reason a filesystem "
                         L"operation can fail.");
    NativeRm api;
    return enumerate(std::move(r), api);
}
std::string utf8(const std::wstring &value) {
    if (value.empty())
        return {};
    if (value.size() > INT_MAX)
        throw std::length_error("Text is too large");
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr,
                                          0, nullptr, nullptr);
    if (!count)
        throw std::runtime_error("UTF-8 conversion failed");
    std::string out(static_cast<std::size_t>(count), '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count,
                             nullptr, nullptr))
        throw std::runtime_error("UTF-8 conversion failed");
    return out;
}
void write_output(HANDLE handle, const std::wstring &value) {
    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode)) {
        std::size_t pos = 0;
        while (pos < value.size()) {
            DWORD done = 0;
            const DWORD n = static_cast<DWORD>(std::min<std::size_t>(value.size() - pos, 16384));
            const BOOL okay = WriteConsoleW(handle, value.data() + pos, n, &done, nullptr);
            if (!okay || !done)
                throw OutputError("WriteConsoleW", okay ? ERROR_WRITE_FAULT : GetLastError());
            pos += done;
        }
    } else {
        const auto bytes = utf8(value);
        std::size_t pos = 0;
        while (pos < bytes.size()) {
            DWORD done = 0;
            const DWORD n = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - pos, 16384));
            const BOOL okay = WriteFile(handle, bytes.data() + pos, n, &done, nullptr);
            if (!okay || !done)
                throw OutputError("WriteFile", okay ? ERROR_WRITE_FAULT : GetLastError());
            pos += done;
        }
    }
}
} // namespace fle
