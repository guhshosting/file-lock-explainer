#ifndef FLE_CORE_H
#define FLE_CORE_H
#include <windows.h>
#include <restartmanager.h>
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace fle {
class OutputError : public std::runtime_error {
  public:
    DWORD windows_code;
    OutputError(const char *operation, DWORD code) : std::runtime_error(operation), windows_code(code) {}
};
enum class ExitCode : std::uint8_t {
    success = 0,
    usage = 2,
    target = 3,
    access = 4,
    inspection = 5,
    internal = 6
};
struct Options {
    bool json = false, help = false, version = false;
    std::wstring path;
};
Options parse(const std::vector<std::wstring> &args);
struct Error {
    std::string operation;
    DWORD code = 0;
    std::wstring message;
};
struct Result {
    std::wstring requested, inspected;
    std::string type = "unknown", state = "failed";
    int exit_code = 0;
    std::vector<RM_PROCESS_INFO> apps;
    std::vector<std::wstring> warnings;
    Error error;
    DWORD reboot_reasons = 0;
};
class RmApi {
  public:
    virtual ~RmApi() = default;
    virtual DWORD start(DWORD &session) = 0;
    virtual DWORD register_file(DWORD session, const wchar_t *path) = 0;
    virtual DWORD list(DWORD session, UINT &needed, UINT &count, RM_PROCESS_INFO *apps, DWORD &reasons) = 0;
    virtual DWORD end(DWORD session) noexcept = 0;
};
std::wstring error_message(DWORD code);
Result enumerate(Result result, RmApi &api);
Result inspect(const std::wstring &path);
std::string utf8(const std::wstring &value);
std::string json(const Result &result);
std::wstring human(const Result &result);
std::string app_type(RM_APP_TYPE type);
std::vector<std::string> status_names(ULONG status);
void write_output(HANDLE handle, const std::wstring &value);
} // namespace fle
#endif
