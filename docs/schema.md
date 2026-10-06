# JSON schema v1

> Release context: see [qualified platforms, runtime requirements and package scope](../RELEASE-QUALIFICATION.md).


Output is one JSON object, even for expected failure modes. Consumers should
ignore new fields, tolerate new enumeration strings, and check `schema_version`.
All documented keys are always present. Optional values are consistently `null`.
Array ordering follows Restart Manager's snapshot; the formatter preserves it.
Object field ordering is deterministic but consumers must not rely on order.

| Field | Type | Description |
| --- | --- | --- |
| schema_version | integer | `1` |
| application_version | string | CMake project version |
| requested_path | string | Exact argument; empty for usage/internal errors without an available path |
| inspected_path | string or null | Absolute DOS path or unchanged extended DOS path; null if unavailable |
| resource_type | string | `file`, `directory`, `unknown` |
| result_state | string | `identified`, `none_identified`, `failed`, `limited` |
| exit_code | integer | Process exit contract |
| associated_application_count | integer | Length of applications |
| reboot_reason_flags | integer | Raw Restart Manager reason mask; informational, never a reboot request |
| applications | array | Restart Manager records; no optional external enrichment |
| warnings | array of strings | Accuracy/limitation notes; localized Windows errors are in error.message |
| error | object or null | Failure detail |

An application has:

| Field | Type | Description |
| --- | --- | --- |
| pid | integer | Restart Manager process ID; not a unique identity alone |
| name | string | Restart Manager application name, possibly empty |
| application_type | string | `unknown`, `main_window`, `other_window`, `service`, `explorer`, `console`, `critical` |
| application_type_value | integer | SDK enumeration value, including future unknown values |
| restartable | boolean | Restart Manager restartability; this tool never restarts anything |
| status_flags | integer | Complete SDK bitmask, including future bits |
| status_names | array of strings | `unknown` for zero; named known flags plus `unknown_bits` when needed |
| service_name | string or null | Restart Manager service short name when present |
| process_start_time_filetime_hex | string | Exactly 16 hexadecimal digits, high DWORD then low DWORD |
| terminal_session_id | integer | SDK terminal session ID, including SDK sentinel values |

Start time is the raw Windows FILETIME identity (100 ns ticks since 1601 UTC).
Hex is used because a 64-bit FILETIME integer can exceed JavaScript's exact
numeric range. Combine it with PID to compare identities. All other numeric
values remain JSON numbers. No metadata is looked up by PID alone.

Known status names: `running`, `stopped`, `stopped_other`, `restarted`,
`error_on_stop`, `error_on_restart`, `shutdown_masked`, `restart_masked`.
These are Restart Manager's flags; do not assume they are mutually exclusive or
infer that this diagnostic performed any control operation.

`error` contains `operation` (string), `windows_code` (integer),
`windows_code_hex` (eight-digit `0x` string), and `message` (string).
Usage/internal errors use code zero because no Windows API error caused them;
`operation` and `exit_code` distinguish these from success. System messages
depend on Windows language; use operation/code, not message text, in scripts.
Unknown/unformattable errors receive a safe explicit fallback message.

Successful empty example:

```json
{"schema_version":1,"application_version":"1.0.0","requested_path":"C:\\work\\data.db","inspected_path":"C:\\work\\data.db","resource_type":"file","result_state":"none_identified","exit_code":0,"associated_application_count":0,"reboot_reason_flags":0,"applications":[],"warnings":["Restart Manager does not identify every open handle or every reason a filesystem operation can fail."],"error":null}
```

Escaping is applied to UTF-16 units, including paired/unpaired surrogates, quotes,
backslashes and controls. Output is valid ASCII JSON, hence valid UTF-8, without
a BOM. Independent Node `JSON.parse` tests verify Unicode values and expected
failure output. Successful serialization does not guarantee delivery if stdout
is closed, unwritable or redirected to a failing output device.
