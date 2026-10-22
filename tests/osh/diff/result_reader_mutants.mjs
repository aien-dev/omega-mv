// Compiled behavioral controls. A build error, signal, deadline or unrelated
// failing assertion is an infrastructure failure and never a killed mutant.
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync} from 'node:fs';
import {join, resolve} from 'node:path';
const [referenceArg, mode = 'pins'] = process.argv.slice(2);
if (!referenceArg || !['pins', 'framing'].includes(mode)) throw Error('usage: node result_reader_mutants.mjs HOST_RESULT [pins|framing]');
const reference = resolve(referenceArg);
const root = mkdtempSync('/tmp/L553-reader-mutants-');
const source = readFileSync('tests/osh/diff/osh_diff.c', 'utf8');
const framingStart = source.indexOf('    for (;;) {\n        /* Read one physical serial line');
const framingEnd = source.indexOf('        if (ended) { rf_invalid');
const mutations = mode === 'pins' ? [
  ['duplicate', '                if (r->pin[pi][0]) { rf_invalid(r, "duplicate pin %s", PIN_KEYS[pi]); break; }', '', 16],
  ['token', 'char *eq = memchr(s, \'=\', token_len);', 'char *eq = strchr(s, \'=\');', 1],
] : [
  ['nul', '        if (has_nul) { rf_invalid(r, "%s", "NUL in OSHDIFF line"); continue; }',
    '        (void)has_nul;', 4],
  ['count', '            if (line[14] < \'0\' || line[14] > \'9\' || *e)',
    '            if (*e)', 4],
  ['read-error', '    if (ferror(f)) rf_invalid(r, "%s", "result file read error");', '', 1],
  ['chunks', source.slice(framingStart, framingEnd),
    `    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\\n' || line[n - 1] == '\\r')) line[--n] = 0;
        if (strncmp(line, "OSHDIFF", 7) != 0) continue;
`, 7],
];
function command(name, program, args) {
  const r = spawnSync(program, args, {encoding:'utf8', timeout:180000, maxBuffer:4 << 20});
  writeFileSync(join(root, `${name}.log`), (r.stdout ?? '') + (r.stderr ?? ''));
  writeFileSync(join(root, `${name}.status.json`), JSON.stringify({status:r.status,signal:r.signal,error:r.error?.message ?? null})+'\n');
  if (r.error || r.signal || r.status === null) throw Error(`${name}: infrastructure failure`);
  return r;
}
for (const [name, from, to, count] of mutations) {
  if (source.split(from).length !== 2) throw Error(`${name}: source anchor is not unique`);
  const mutant = join(root, `${name}.c`), out = join(root, name);
  writeFileSync(mutant, source.replace(from, to));
  const build = command(`${name}-build`, 'make', ['-s','-j2',`OUT_DIR=${out}`,`OSH_DIFF_SRCS=${mutant} $(OSH_DIR)/osh_units.c $(filter-out src/osh/host/osh_main.c,$(OSH_PROG_HOSTSRCS)) $(OSC_LIB) src/runtime/rx_caproot.c`, `${out}/osh/osh_diff`, `${out}/osh/osh_toy`]);
  if (build.status !== 0) throw Error(`${name}: compile failure, never a kill`);
  const probe = command(`${name}-test`, process.execPath, ['tests/osh/diff/result_reader_controls.mjs', `${out}/osh/osh_diff`, `${out}/osh/osh_toy`, 'tests/osh/diff/fixtures.txt', join(root, `${name}-cases`), mode, reference]);
  const failures = probe.stdout.split('\n').filter(l=>l.startsWith('FAIL '));
  const expected = failures.every(l => {
    if (name === 'duplicate') return l.startsWith('FAIL duplicate-');
    if (name === 'token') return l.startsWith('FAIL key-without-equals:');
    if (name === 'nul') return l.startsWith('FAIL nul-');
    if (name === 'count') return l.startsWith('FAIL end-');
    if (name === 'chunks') return /^FAIL (nul-|overlong-protocol:|chunk-injected-records:|line-size-overflow:)/.test(l);
    return l.startsWith('FAIL read-error:');
  });
  if (probe.status !== 1 || failures.length !== count || !expected || !probe.stdout.includes(`${count} failed; synthetic native controls only`)) throw Error(`${name}: wrong behavioral verdict: ${failures.join('; ')}`);
  console.log(`PASS ${name}: compiled, ${count} exact expected failing assertions`);
}
console.log(`RESULT_READER_MUTANTS_${mode.toUpperCase()}: ${mutations.length} killed; artifacts ${root}`);
