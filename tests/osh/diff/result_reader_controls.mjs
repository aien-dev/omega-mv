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
console.log(`RESULT_READER_${mode.toUpperCase()}: ${passed} passed, ${failed} failed; synthetic native controls only`);
process.exitCode = failed ? 1 : 0;
