// #568: exercise the real result-file reader through its CLI. All native-looking
// files below are SYNTHETIC comparator controls, never evidence of native execution.
import {spawnSync} from 'node:child_process';
import {readFileSync, writeFileSync, mkdirSync} from 'node:fs';
import {resolve, join} from 'node:path';
const [runnerArg, toyArg, fxArg, outArg, mode = 'pins', referenceArg] = process.argv.slice(2);
if (!outArg || !['pins', 'framing'].includes(mode)) throw Error('usage: node result_reader_controls.mjs RUNNER TOY FIXTURES FRESH_OUT [pins|framing]');
const runner = resolve(runnerArg), toy = resolve(toyArg), fx = resolve(fxArg), out = resolve(outArg);
mkdirSync(out, {recursive: false});
let failed = 0, passed = 0;
function run(name, args) {
  const r = spawnSync(runner, args, {encoding: 'utf8', timeout: 20000, maxBuffer: 4 << 20});
  writeFileSync(join(out, `${name}.out`), (r.stdout ?? '') + (r.stderr ?? ''));
  writeFileSync(join(out, `${name}.status.json`), JSON.stringify({status:r.status, signal:r.signal, error:r.error?.message ?? null}) + '\n');
  if (r.error || r.signal || r.status === null) throw Error(`${name}: infrastructure failure, never a behavioral kill`);
  if (/Sanitizer|runtime error:/.test(r.stderr ?? '')) throw Error(`${name}: sanitizer diagnostic, never a behavioral kill`);
  return r;
}
function check(name, r, status, required) {
  const ok = r.status === status && required.every(s => r.stdout.includes(s));
  console.log(`${ok ? 'PASS' : 'FAIL'} ${name}: exit=${r.status}, expected=${status}`);
  if (ok) passed++; else failed++;
}
const hostPath = join(out, 'actual-host.result');
if (referenceArg) {
  // Sanitizer/mutation legs exercise only the reader against the plain leg's
  // actual host result. This mode makes no host-execution or native-run claim.
  writeFileSync(hostPath, readFileSync(resolve(referenceArg)));
} else {
  const host = run('actual-host', ['--strict-host', '--emit', hostPath, toy, fx]);
  check('actual-host', host, 0, ['summary: host PASS 66 FAIL 0 NOT_RUN 0', 'aienos PASS 0 FAIL 0 NOT_RUN 33', 'OSH_DIFF_PASS']);
  if (host.status !== 0) throw Error('actual host execution failed; no comparator reference');
}
const reference = readFileSync(hostPath, 'utf8');
const synthetic = reference.replace('adapter=linux-host ', 'adapter=aienos-native ').replace(/aienos=\S+/, 'aienos=synthetic-control-only');
function compare(name, bytes, status, required, extra = []) {
  const path = join(out, `${name}.result`);
  writeFileSync(path, bytes);
  const r = run(name, ['--host-results', hostPath, '--native', path, ...extra, toy, fx]);
  check(name, r, status, required);
}
const good = ['aienos PASS 28 FAIL 0 NOT_RUN 5', 'OSH_DIFF_COMPARE_ONLY_PASS'];
compare('synthetic-complete', synthetic, 0, good, ['--require-native']);
compare('serial-crlf', `boot noise\r\n${synthetic.replaceAll('\n', '\r\n')}prompt\r\n`, 0, good);
if (mode === 'pins') {
  for (const key of ['adapter', 'aienos', 'omega', 'core', 'abi', 'cc', 'harness', 'fixtures']) {
    const value = synthetic.match(new RegExp(` ${key}=(\\S+)`))[1];
    for (const [kind, value0] of [['same', value], ['conflict', 'conflicting-control']]) {
      const bad = synthetic.replace(` ${key}=${value}`, ` ${key}=${value0} ${key}=${value}`);
      compare(`duplicate-${key}-${kind}`, bad, 1, ['INVALID result file', `duplicate pin ${key}`, 'aienos PASS 0 FAIL 28', 'OSH_DIFF_COMPARE_ONLY_FAIL']);
    }
  }
  compare('bare-token', synthetic.replace(/(OSHDIFF pin[^\n]*)/, '$1 bare'), 1, ['INVALID result file', 'pin token without =', 'OSH_DIFF_COMPARE_ONLY_FAIL']);
  compare('missing-value', synthetic.replace(/ cc=\S+/, ' cc='), 1, ['INVALID result file', 'empty or long pin value for cc', 'OSH_DIFF_COMPARE_ONLY_FAIL']);
  compare('key-without-equals', synthetic.replace(/ cc=\S+/, ' cc broken=value'), 1, ['INVALID result file', 'pin token without =', 'OSH_DIFF_COMPARE_ONLY_FAIL']);
  compare('unknown-key', synthetic.replace('OSHDIFF pin ', 'OSHDIFF pin other=value '), 1, ['INVALID result file', 'unknown pin key', 'OSH_DIFF_COMPARE_ONLY_FAIL']);
}
if (mode === 'framing') {
  for (const [name, bytes] of [
    ['nul-header', synthetic.replace('OSHDIFF-RESULT 1\n', 'OSHDIFF-RESULT 1\0ignored\n')],
    ['nul-pin', synthetic.replace(/(OSHDIFF pin[^\n]*)/, '$1\0ignored')],
    ['nul-vector', synthetic.replace(/(OSHDIFF vec[^\n]*)/, '$1\0ignored')],
    ['nul-end', synthetic.replace(/(OSHDIFF end[^\n]*)/, '$1\0ignored')],
  ]) compare(name, bytes, 1, ['INVALID result file', 'NUL in OSHDIFF line', 'aienos PASS 0 FAIL 28', 'OSH_DIFF_COMPARE_ONLY_FAIL']);
  compare('overlong-protocol', synthetic.replace('OSHDIFF-RESULT 1\n', `OSHDIFF-RESULT 1${'\0'.repeat(16384)}\n`), 1, ['INVALID result file', 'OSHDIFF line too long', 'OSH_DIFF_COMPARE_ONLY_FAIL']);
  // Old fgets framing sees each 16383-byte chunk as a new physical line and
  // accepts these protocol records hidden inside ordinary serial-noise lines.
  const hidden = synthetic.split('\n').filter(Boolean).map(l => 'x'.repeat(16383) + l).join('\n') + '\n';
  compare('chunk-injected-records', hidden, 1, ['INVALID result file', 'no "OSHDIFF-RESULT 1" header', 'OSH_DIFF_COMPARE_ONLY_FAIL']);
  compare('long-serial-noise', `${'x'.repeat(50000)}OSHDIFF-RESULT bogus\n${synthetic}`, 0, good);
  compare('unterminated-final-line', synthetic.trimEnd(), 0, good);
  compare('line-size-boundary', synthetic.replace('OSHDIFF-RESULT 1\n', `OSHDIFF-RESULT 1\n${'OSHDIFF '.padEnd(16383, 'x')}\n`), 1, ['unknown OSHDIFF line:', 'OSH_DIFF_COMPARE_ONLY_FAIL']);
  compare('line-size-overflow', `${'OSHDIFF '.padEnd(16384, 'x')}\n${synthetic}`, 1, ['OSHDIFF line too long', 'OSH_DIFF_COMPARE_ONLY_FAIL']);
  const directory = join(out, 'read-error-directory');
  mkdirSync(directory);
  check('read-error', run('read-error', ['--host-results', hostPath, '--native', directory, toy, fx]), 1,
    ['INVALID result file', 'result file read error', 'OSH_DIFF_COMPARE_ONLY_FAIL']);
  for (const [name, count] of [['plus', '+33'], ['space', ' 33'], ['minus-zero', '-0'], ['empty', '']]) {
    compare(`end-${name}`, synthetic.replace(/OSHDIFF end n=\d+/, `OSHDIFF end n=${count}`), 1, ['INVALID result file', 'invalid end line count', 'OSH_DIFF_COMPARE_ONLY_FAIL']);
  }
}
console.log(`RESULT_READER_${mode.toUpperCase()}: ${passed} passed, ${failed} failed; synthetic native controls only`);
process.exitCode = failed ? 1 : 0;
