'use strict';
// Node is a development-only independent JSON parser and process orchestrator.
const fs = require('fs');
const os = require('os');
const path = require('path');
const cp = require('child_process');
const assert = require('assert');
const [exe, helper, source] = process.argv.slice(2);
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'fle tests 雪 '));
const file = path.join(temp, 'multiple.periods 雪.txt');
fs.writeFileSync(file, 'read-only acceptance fixture');
function run(args, code = 0, cwd = temp) {
  const p = cp.spawnSync(exe, args, { encoding: 'utf8', timeout: 15000, cwd, windowsHide: true });
  assert.ifError(p.error); assert.strictEqual(p.status, code, `${args}: ${p.stderr} ${p.stdout}`);
  return p;
}
function json(args, code = 0) {
  const p = run(['--json', ...args], code); assert.strictEqual(p.stderr, '');
  const r = JSON.parse(p.stdout);
  assert.strictEqual(r.schema_version, 1); assert.strictEqual(r.application_version, '1.0.0');
  assert.strictEqual(r.exit_code, code); assert.strictEqual(r.associated_application_count, r.applications.length);
  return r;
}
function ready(child) {
  return new Promise((resolve, reject) => {
    let text = ''; const timer = setTimeout(() => reject(new Error('helper readiness timeout')), 10000);
    child.stdout.on('data', b => { text += b.toString(); if (text.includes('\n')) { clearTimeout(timer); resolve(Number(text.trim().split(' ')[1])); } });
    child.on('error', e => { clearTimeout(timer); reject(e); });
    child.on('exit', code => { if (!text.includes('\n')) { clearTimeout(timer); reject(new Error(`helper exited ${code}`)); } });
  });
}
function exited(child) {
  return new Promise((resolve, reject) => {
    if (child.exitCode !== null) return resolve(child.exitCode);
    const timer = setTimeout(() => reject(new Error('helper exit timeout')), 10000);
    child.once('exit', code => { clearTimeout(timer); resolve(code); });
  });
}
async function main() {
  assert.match(run(['--help']).stdout, /Usage:/);
  assert.strictEqual(run(['--version']).stdout.trim(), 'file-lock-explainer 1.0.0');
  run([], 2); run(['--unknown'], 2); json([], 2); json(['--unknown'], 2);
  const empty = json([file]); assert.strictEqual(empty.result_state, 'none_identified');
  assert.match(run([file]).stdout, /No applications or services/);
  assert.strictEqual(json(['.\\multiple.periods 雪.txt']).inspected_path, file);
  fs.writeFileSync(path.join(temp, '-file'), 'x'); json(['--', '-file']);
  const missing = json([path.join(temp, 'missing')], 3); assert.strictEqual(missing.error.operation, 'CreateFileW metadata');
  assert.strictEqual(json([temp], 3).resource_type, 'directory');
  const probe = cp.spawnSync(path.join(path.dirname(exe), 'rm-probe.exe'), [temp], { encoding: 'utf8', timeout: 15000, windowsHide: true });
  assert.ifError(probe.error); assert.strictEqual(probe.status, 4);
  const directoryProbe = JSON.parse(probe.stdout);
  assert.strictEqual(directoryProbe.error.operation, 'RmGetList');
  assert.strictEqual(directoryProbe.error.windows_code, 5); // Documented directory semantics, not a permissions diagnosis.
  assert.strictEqual(json(['\\\\example.invalid\\share\\file'], 3).error.windows_code, 50);
  assert.strictEqual(json(['\\\\?\\UNC\\example.invalid\\share\\file'], 3).error.windows_code, 50);
  assert.strictEqual(json(['\\\\.\\PhysicalDrive0'], 3).error.windows_code, 50);
  for (const device of ['NUL', 'nul.txt', 'CON', 'PRN', 'AUX', 'COM1', 'LPT9', 'COM¹', 'CONOUT$']) {
    assert.strictEqual(json([path.join(temp, device)], 3).error.operation, 'DOS device name refused');
  }
  // A local junction is enough to verify conservative reparse refusal; no network target is contacted.
  const junction = path.join(temp, 'junction'); fs.symlinkSync(temp, junction, 'junction');
  assert.strictEqual(json([path.join(junction, path.basename(file))], 3).error.operation, 'Reparse point refused');
  fs.unlinkSync(junction);
  const extended = '\\\\?\\' + file; json([extended]);
  const special = '\\\\?\\' + path.join(temp, 'trailing. '); fs.writeFileSync(special, 'x'); json([special]); fs.unlinkSync(special);
  let longDir = temp;
  for (let i = 0; i < 7; ++i) { longDir = path.join(longDir, 'a'.repeat(40)); fs.mkdirSync(longDir); }
  const longFile = path.join(longDir, 'long.txt'); fs.writeFileSync(longFile, 'x');
  const longResult = cp.spawnSync(exe, ['--json', '\\\\?\\' + longFile], { encoding: 'utf8', timeout: 15000, windowsHide: true });
  assert.ifError(longResult.error);
  const longError = JSON.parse(longResult.stdout);
  if (longResult.status !== 0) {
    assert.strictEqual(longResult.status, 5);
    assert.strictEqual(longError.error.operation, 'RmRegisterResources');
    assert.strictEqual(longError.error.windows_code, 29); // Observed RM limitation; never rewrite resource semantics.
    console.log('Long-path registration rejected by Restart Manager with ERROR_WRITE_FAULT');
  }
  const before = fs.readFileSync(file); const stat = fs.statSync(file);
  const child = cp.spawn(helper, [file], { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true });
  try {
    const pid = await ready(child);
    const locked = json([file]);
    assert(locked.applications.some(a => a.pid === pid), `Restart Manager did not report helper ${pid}: ${JSON.stringify(locked)}`);
    const a = locked.applications.find(a => a.pid === pid);
    assert.match(a.process_start_time_filetime_hex, /^[0-9a-f]{16}$/);
    assert.match(run([file]).stdout, new RegExp(`PID: ${pid}`));
    // Release concurrently with an inspection; either completed snapshot is legitimate.
    const racing = cp.spawn(exe, ['--json', file], { windowsHide: true });
    let text = ''; racing.stdout.on('data', b => { text += b; });
    const completion = exited(racing);
    child.stdin.end(); assert.strictEqual(await exited(child), 0);
    assert.strictEqual(await completion, 0); JSON.parse(text);
    assert.strictEqual(json([file]).result_state, 'none_identified');
  } finally { child.stdin.end(); if (child.exitCode === null) await exited(child); }
  assert.deepStrictEqual(fs.readFileSync(file), before);
  assert.strictEqual(fs.statSync(file).mtimeMs, stat.mtimeMs);
  fs.unlinkSync(file); json([file], 3); // disappearing resource
  const fixtures = cp.spawnSync(path.join(path.dirname(exe), 'unit-tests.exe'), [], { encoding: 'utf8', timeout: 10000 });
  assert.strictEqual(fixtures.status, 0, fixtures.stderr);
  const models = fixtures.stdout.trim().split(/\r?\n/).map(line => JSON.parse(line));
  assert.deepStrictEqual(Object.keys(models[0]).sort(), ['schema_version','application_version','requested_path','inspected_path','resource_type','result_state','exit_code','associated_application_count','reboot_reason_flags','applications','warnings','error'].sort());
  assert.strictEqual(models[0].requested_path, 'quote" slash\\ line\n tab\t\u0000🔒');
  assert.strictEqual(models[0].applications.length, 1);
  assert.strictEqual(models[0].applications[0].name, 'service 雪"\\\n');
  assert.strictEqual(models[0].applications[0].service_name, 'Svc');
  assert.strictEqual(models[0].applications[0].restartable, true);
  assert.strictEqual(models[0].applications[0].status_names.length, 8);
  assert.strictEqual(models[1].applications.length, 2);
  assert.strictEqual(models[1].applications[1].restartable, false);
  assert.strictEqual(models[1].applications[1].application_type, 'unknown');
  assert.deepStrictEqual(models[1].applications[1].status_names, ['unknown_bits']);
  // Review production sources, not test helpers which intentionally create fixtures.
  const production = fs.readdirSync(path.join(source, 'src')).filter(n => /\.(cpp|h)$/.test(n))
    .map(n => fs.readFileSync(path.join(source, 'src', n), 'utf8')).join('\n');
  const prohibited = /\b(RmShutdown|RmRestart|TerminateProcess|SuspendThread|ResumeThread|DebugActiveProcess|OpenProcess|DuplicateHandle|CreateRemoteThread\w*|WriteProcessMemory|VirtualAllocEx|AdjustTokenPrivileges|ControlService|StartService\w*|ChangeServiceConfig\w*|DeleteService|DeleteFile\w*|MoveFile\w*|ReplaceFile\w*|CreateDirectory\w*|SetFileAttributes\w*|SetFileSecurity\w*|SetNamedSecurityInfo\w*|SetSecurityInfo|SetFileInformationByHandle|SetEndOfFile|RegSetValue\w*|RegCreateKey\w*|RegDelete\w*|RegLoadKey\w*|SetEnvironmentVariable\w*|InitiateSystemShutdown\w*|ExitWindowsEx|CreateProcess\w*|ShellExecute\w*|WinExec|system|_wsystem|_popen|_wpopen|LoadLibrary\w*|GetProcAddress|WinHttp\w*|Internet\w*|WSA\w*|socket|connect|getaddrinfo|GetAddrInfo\w*|URLDownload\w*)\s*\(/;
  assert(!prohibited.test(production), 'prohibited production API');
  assert.strictEqual((production.match(/\bCreateFileW\s*\(/g) || []).length, 1);
  assert(production.replace(/\s+/g, '').includes('CreateFileW(part.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)'));
  console.log('CLI, independent JSON, Unicode/space/long paths, real lock, exit race, directory/refusal and safety acceptance passed');
}
main().finally(() => fs.rmSync(temp, { recursive: true, force: true })).catch(e => { console.error(e); process.exitCode = 1; });
