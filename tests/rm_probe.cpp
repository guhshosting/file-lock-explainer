#include "core.h"
class Api final : public fle::RmApi {
  public:
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
// Keep the documented MSVC Unicode entry-point signature.
// cppcheck-suppress constParameter
int wmain(int argc, wchar_t *argv[]) {
    if (argc != 2)
        return 2;
    fle::Result r;
    r.requested = argv[1];
    r.inspected = argv[1];
    Api api;
    const auto result = fle::enumerate(std::move(r), api);
    const auto text = fle::json(result);
    fle::write_output(GetStdHandle(STD_OUTPUT_HANDLE), {text.begin(), text.end()});
    return result.exit_code;
}
