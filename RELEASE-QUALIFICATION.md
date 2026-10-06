# Validation and release context: file-lock-explainer 1.0.0

Development/build validation: Windows Server 2025 x64. The exact executable completed physical Windows 11 Pro 25H2 x64 qualification (build 26200.9550) and Windows 10 Pro 22H2 x64 qualification in a dedicated QEMU VM (build 19045.6466). Physical Windows 10 hardware and ARM64 were not qualified. Windows 11 completion is user supplied with retained raw results; the consolidated report is unavailable. Historical Windows 10 detailed ledgers are unavailable; completion is user supplied.

The final ZIP was assembled after executable qualification around the unchanged qualified EXE. Execution of this final assembled package on Windows is not claimed. Development validation is distinct from client qualification.

Qualified executable SHA-256: `5e12ea6ae2138a07b4916001eb7dc659a433e46a97c4b424194228c10f729545`.

No separately imported MSVC C++ runtime DLLs were found; Windows system libraries remain required.

The original Windows 10 Codex-sandbox qualification was blocked: RmStartSession returned Win32 error 29 (ERROR_WRITE_FAULT). That sandbox run did not pass. The exact unchanged EXE subsequently passed manual testing outside the sandbox in normal unelevated PowerShell: unlocked result_state none_identified, exit 0, count 0; controlled exclusive lock result_state identified, exit 0, count 2; after lock release the final unlocked recheck passed. This manual result establishes the final PASS; it does not rewrite the sandbox result.

See application documentation for query, output and safety limits. Windows may perform internal activity while servicing read-only API calls.
