#include <iostream>
#include <windows.h>
int wmain(int argc, wchar_t *argv[]) {
    if (argc != 2)
        return 2;
    HANDLE file = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return 3;
    std::cout << "ready " << GetCurrentProcessId() << '\n' << std::flush;
    // Parent closes stdin to release. Parent/test timeout is a second bound.
    char c = 0;
    std::cin.get(c);
    CloseHandle(file);
    return 0;
}
