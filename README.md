# file-lock-explainer

> Release context: see [qualified platforms, runtime requirements and package scope](RELEASE-QUALIFICATION.md).


Windows-native command-line diagnostics for applications and services associated
with a local file being in use. Version 1.0.0 uses the supported Windows Restart
Manager API, with no third-party runtime or production dependency beyond Windows.
It intentionally does not forcibly unlock files.

```text
file-lock-explainer "C:\work\data.db"
file-lock-explainer --json "C:\work\report 雪.txt"
file-lock-explainer -- -unusual-name.txt
file-lock-explainer --help
file-lock-explainer --version
```

Options may precede or follow the path. `--` ends option parsing. Exactly one
nonempty path is required. Help and version must be used alone.

Example human output (names and status come from Restart Manager):

```text
Resource: C:\work\data.db
Associated applications/services: 1

1. Example Application
   PID: 1234
   Type: Console
   Restartable (Restart Manager): No
   Status: running
   Process start identity (FILETIME hex): 01dce12301234567
Note: Restart Manager does not identify every open handle or every reason a filesystem operation can fail.
```

An empty successful result says:

```text
No applications or services were identified for this resource through Windows Restart Manager.
```

**No applications identified does not prove that the resource has no open
handles or that another filesystem condition is not preventing an operation.**
Restart Manager is an association mechanism, not a universal lock detector.
Access permissions, filesystem conditions, drivers and other users of a resource
can lie outside its coverage. Results are a snapshot and can become stale immediately.

## Supported platforms

Intended targets: Windows 10, Windows 11, and corresponding supported Windows
Server generations (Server 2016 onward). Older Windows versions are outside
the support contract. x64 is the validated build architecture; other architectures
require their own build and runtime validation.

The production APIs predate Windows 10; compilation explicitly targets Windows 10
with `_WIN32_WINNT` and `WINVER` set to `0x0A00`. The manifest uses `asInvoker`
and Windows 10 compatibility and enables long path awareness. There is no
Server 2025-only feature dependency. Windows 10/client/other Server compatibility
remains pending real-machine validation; successful Server 2025 compilation is
not proof of compatibility. See [validation report](docs/validation.md).

## Path behavior and limits

Relative DOS paths become absolute through `GetFullPathNameW`; there is no
filesystem canonicalization or symlink resolution. Extended `\\?\C:\...` paths
are preserved exactly. Ordinary DOS names retain ordinary Windows namespace
semantics; use an extended path when literal trailing dots/spaces are intended.
Unicode, spaces and multiple periods are supported.

Directories are accepted as inputs but reported as unsupported (exit 3). They
are not recursively inspected. The real API probe verifies the documented
`RmGetList` directory failure rather than suggesting directory association coverage.

UNC paths, extended UNC, device namespaces, network drive mappings, SUBST/other
drive aliases, and all paths crossing reparse points (including local symlinks,
junctions and cloud placeholders) are refused before any remote resource
validation. Only ordinary local disk/CD/RAM volume mappings are accepted.
Reserved DOS device names are rejected before opening them, including extension
and superscript-port forms; literal names in the extended DOS filesystem namespace
remain subject to normal metadata and Restart Manager validation.
This conservative policy is deliberate: accepting a UNC string must not cause a
network access. No DNS lookup is used to decide whether a host exists.

Metadata-only `OPEN_EXISTING` handles use `FILE_READ_ATTRIBUTES` and
`FILE_FLAG_OPEN_REPARSE_POINT` for each component, sharing read/write but not
delete. They are held only during inspection to prevent ordinary component
rename/delete races from redirecting inspection to a network location. This can
briefly delay a concurrent rename/delete, or cause validation to fail if an
existing delete-access handle conflicts. No file data is read or written.
Changes by privileged filesystem drivers or concurrent volume remapping cannot
be completely controlled by a userspace diagnostic.

Long paths reach the API unchanged. On the tested Server 2025 system a short
extended path and a literal trailing-dot/space filename work, while an extended
path over `MAX_PATH` fails at `RmRegisterResources` with `ERROR_WRITE_FAULT`.
The utility preserves that error; it never substitutes a different filename or
uses short-name rewriting to conceal an unsupported resource form.

## Safety, privacy and privileges

The executable requests no elevation, enables no privileges and makes no process
or service control calls. It never invokes shutdown/restart operations, closes
another process's handles, edits file contents, changes permissions, creates
temporary files, changes application settings, installs components or sends
telemetry. Output goes only to stdout/stderr; there is no implicit output file.
Optional process enrichment is intentionally omitted: PID and start-time identity
are presented directly from Restart Manager, avoiding PID reuse mistakes.

**API constraint:** a literal guarantee of no Registry state changes is incompatible
with the required Restart Manager API. Microsoft documents internal Registry
writes during resource registration. This application uses only `RmStartSession`,
`RmRegisterResources`, `RmGetList`, and `RmEndSession`, and ends acquired sessions
through RAII. It performs no direct Registry writes and creates no deliberate
persistent application state. Restart Manager's internal state is managed by
Windows. If even these Windows-managed writes are forbidden, this program cannot
meet that policy; the primary mechanism would have to change.

There is no intentional network functionality, update checking, online account,
metadata lookup or telemetry; no networking library is linked. Remote resources
are refused as above. Windows itself and filesystem providers may have independent
system activity; this is not a guarantee about all activity on the computer.

Access restrictions produce specific Windows errors. Administrator rights are
not a requirement. A normal-user runtime test still needs to be performed on a
separate unelevated desktop/session before public release.

## JSON and exit codes

`--json` produces one JSON object followed by a newline and nothing else on stdout,
including expected usage, target and Windows API errors. Text diagnostics use
stderr on failure. Redirected text is UTF-8; attached console text uses Unicode
console APIs. JSON is ASCII with Unicode escaped losslessly as UTF-16 units.
The versioned schema is documented in [docs/schema.md](docs/schema.md).

| Code | Meaning |
| --- | --- |
| 0 | Inspection completed, with or without associated applications |
| 2 | Invalid command-line usage |
| 3 | Invalid, absent, unavailable or intentionally unsupported target/path |
| 4 | Access restriction prevented inspection |
| 5 | Restart Manager/Windows API inspection or cleanup failure, including retry exhaustion |
| 6 | Unexpected internal failure or output failure |

Finding applications is not an error. Race exhaustion is `limited`, never a
successful empty result; no untrusted partial association list is returned.
`RmGetList` retries at most eight times with at most 65,536 records. Session
startup and registration failures are not retried indefinitely. Output device
failure can prevent any error object from being delivered; exit 6 remains the
contract in that case.

## Build, test and install

Install a recent MSVC C++ toolchain, Windows SDK, CMake 3.25+ and Ninja. Use an
x64 Native Tools developer prompt. Node.js is needed **only for tests**, to parse
JSON independently and orchestrate controlled native test helpers. There are no
downloads or dependency fetches in CMake.

```text
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --install build/release --prefix out/portable
```

The portable Release executable uses the static MSVC runtime. Install rules place
it in `bin` and documentation/license in `share/file-lock-explainer`.
For a production-only build with no Node requirement:

```text
cmake -S . -B build/portable -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/portable
```

CTest includes CLI, schema/escaping/model tests, injected Restart Manager failures
and changing-list races, real directory API behavior, and a real file-in-use
helper. The helper uses an existing test-owned temporary file; stdin EOF releases
it cleanly. Tests never terminate unrelated processes and have bounded timeouts.
All fixture writes are in the test suite, not in the production executable.
Tests must run in a context that permits Windows-managed Restart Manager session
state. A restrictive sandbox can cause `RmStartSession` to fail with error 29.
Do not disable or skip the real integration test to conceal this restriction.

## Analysis and release review

Project code builds with `/W4 /WX /permissive- /utf-8`. Analysis and sanitizer
configurations are separate from the shipping Release binary:

```text
cmake --preset analyze
cmake --build --preset analyze
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
clang-tidy -p build/release src/core.cpp src/format.cpp src/main.cpp
cppcheck --enable=warning,style,performance,portability --error-exitcode=1 --std=c++17 --platform=win64 -Isrc -Ibuild/release/generated src tests
```

Run clang-tidy in the same developer prompt so SDK include paths are available.
The checked-in `.clang-tidy` selects meaningful bug, analyzer, performance and
portability checks. AddressSanitizer uses `RelWithDebInfo` to avoid incompatible
Debug `/RTC` checks; its runtime DLL is development-only. CTest adds the compiler
runtime directory to its own test environment, not to persistent system settings.

Source-level acceptance tests check prohibited control/mutation/network API calls
and the exact metadata-only file-open contract. Review all production changes for
equivalent APIs, indirect wrappers, new imports, and output handles: a source
scan is a regression guard, not a formal proof. Compare inspected-file contents
and last-write timestamps, audit executable imports, and use a network trace on
a release-validation machine when available. No live network trace or comprehensive
OS Registry trace has been performed in this environment.

Before public release, repeat tests unelevated on supported client/Server versions,
check real applications/services beyond the helper, verify packaging/signing,
and review the remaining validation gaps in the report. No installer, background
service, shell extension, watch loop or remediation feature is included.

## API references

- [RmRegisterResources](https://learn.microsoft.com/en-us/windows/win32/api/restartmanager/nf-restartmanager-rmregisterresources): full filenames and internal Registry-write behavior.
- [RmGetList](https://learn.microsoft.com/en-us/windows/win32/api/restartmanager/nf-restartmanager-rmgetlist): sizing, changing lists and directory limitations.
- [RM_PROCESS_INFO](https://learn.microsoft.com/en-us/windows/win32/api/restartmanager/ns-restartmanager-rm_process_info): association fields and status.

MIT licensed; see [LICENSE](LICENSE).
