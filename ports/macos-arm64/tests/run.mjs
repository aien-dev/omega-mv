// External qualification harness only. No target construction or foreign fallback.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { checkMacho } from './check-macho.mjs';
const root = path.resolve(new URL('..', import.meta.url).pathname);
const build = path.join(root, 'build');
const evidence = path.join(build, 'qualification');
fs.mkdirSync(evidence, { recursive: true });
const compiler = '/home/drakestapleton/handoffs/omega-developer-gaps/shared/omc-191b776e';
const compilerHash = '191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9';
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
const pins = [], results = [];
function save(name, data) { fs.writeFileSync(path.join(evidence, name), data); }
function run(exe, args = [], options = {}) {
  const r = spawnSync(exe, args, { timeout: 60000, maxBuffer: 4 * 1024 * 1024, ...options });
  assert.ifError(r.error); assert.equal(r.signal, null, `${exe}: signal`);
  return r;
}
function test(name, action) {
  action(); results.push(name); console.log(`PASS ${name}`);
}
function pin(name, data) { pins.push(`${sha(data)}  ${name}`); }
function compile(name, source) {
  assert.equal(sha(fs.readFileSync(compiler)), compilerHash, 'hash immediately before EVERY compiler consumption');
  const r = run('quiet-build', ['timeout', '60', compiler], { input: source });
  save(`${name}.compile.stderr`, r.stderr); save(`${name}.compile.exit`, String(r.status) + '\n');
  assert.equal(r.status, 0, `${name}: successful compilation required: ${r.stderr}`);
  assert.ok(r.stdout.length >= 212, 'nonempty compiled control before any runtime/mutation count');
  assert.equal(r.stdout.readUInt32LE(0), 0x464c457f);
  assert.equal(r.stdout.readUInt16LE(18), 183, 'compiled artifact really ARM64');
  const file = path.join(evidence, `${name}.elf`);
  fs.writeFileSync(file, r.stdout, { mode: 0o755 }); fs.chmodSync(file, 0o755);
  pin(`${name}.om`, source); pin(`${name}.elf`, r.stdout);
  return r.stdout;
}
function adapt(input, executable = path.join(build, 'adapter')) {
  return run(executable, [], { input });
}
function refuse(name, input, status, executable) {
  test(name, () => {
    const r = adapt(input, executable);
    assert.equal(r.status, status); assert.equal(r.stdout.length, 0, 'no output before validation');
    save(`${name}.exit`, String(r.status) + '\n');
    save(`${name}.stderr`, r.stderr);
    const infile = path.join(evidence, `${name}.input`), outfile = path.join(evidence, `${name}.preserved`);
    fs.writeFileSync(infile, input); fs.writeFileSync(outfile, 'L615 existing output\n');
    const s = run(path.join(root, 'target.sh'), [infile, outfile]);
    assert.equal(s.status, status); assert.equal(fs.readFileSync(outfile, 'utf8'), 'L615 existing output\n');
    assert.equal(fs.readdirSync(evidence).filter(x => x.startsWith(`${name}.preserved.L615.`)).length, 0);
  });
}
const fixtures = [
  ['return42', 42], ['sum', 45], ['branch', 23], ['overflow', 101],
  ['bitwise', 58], ['divide', 25], ['divzero', 102], ['shift', 24], ['hello', 0],
];
const inputs = new Map();
assert.equal(process.arch, 'arm64', 'Linux native controls require ARM64 host');
assert.equal(process.platform, 'linux');
for (const [name, status] of fixtures) {
  const source = fs.readFileSync(path.join(root, 'tests', `${name}.om`));
  const input = compile(name, source); inputs.set(name, input);
  test(`native Linux seed fixture ${name} exit ${status}`, () => {
    const r = run(path.join(evidence, `${name}.elf`));
    save(`${name}.native.stdout`, r.stdout); save(`${name}.native.stderr`, r.stderr);
    save(`${name}.native.exit`, String(r.status) + '\n');
    assert.equal(r.status, status); assert.equal(r.stderr.length, 0);
    assert.equal(r.stdout.toString(), name === 'hello' ? 'hello omega mac\n' : '');
  });
  test(`target ${name}: independent format + byte preservation + LLVM`, () => {
    const r = adapt(input);
    save(`${name}.adapt.exit`, String(r.status) + '\n'); save(`${name}.adapt.stderr`, r.stderr);
    assert.equal(r.status, 0, `${name} adapter refused`); checkMacho(r.stdout, input, name === 'hello');
    save(`${name}.macho`, r.stdout); pin(`${name}.macho`, r.stdout);
    const parse = run('/usr/bin/llvm-objdump-18', ['--macho', '--private-headers', path.join(evidence, `${name}.macho`)]);
    save(`${name}.llvm.stdout`, parse.stdout); save(`${name}.llvm.stderr`, parse.stderr);
    assert.equal(parse.status, 0); assert.equal(parse.stderr.length, 0);
    assert.match(parse.stdout.toString(), /ARM64/); assert.match(parse.stdout.toString(), /LC_MAIN/);
    assert.deepEqual(adapt(input).stdout, r.stdout, 'deterministic output');
  });
}
const integer = inputs.get('return42'), hello = inputs.get('hello');
const word = (b, p, w) => { const c = Buffer.from(b); c.writeUInt32LE(w >>> 0, p); return c; };
refuse('empty', Buffer.alloc(0), 21);
refuse('truncated header', integer.subarray(0, 175), 21);
refuse('truncated program', integer.subarray(0, -4), 21);
refuse('partial word', integer.subarray(0, -1), 21);
refuse('oversized', Buffer.alloc(16385), 21);
refuse('wrong magic', word(integer, 0, 0), 22);
refuse('wrong ISA', word(integer, 16, 0x3e0002), 23);
refuse('wrong endian', word(integer, 4, 0x010202), 24);
refuse('claimed relocation table', word(integer, 40, 64), 24);
refuse('ELF entry address', word(integer, 24, 0x4000b4), 24);
refuse('segment size', word(integer, 96, 0), 24);
refuse('global storage', word(integer, 160, 0x800008), 24);
refuse('bootstrap stack', word(integer, integer.length - 24, 0xd280001c), 25);
refuse('bootstrap branch', word(integer, 176, 0x14000000), 25);
refuse('bootstrap call', word(integer, integer.length - 12, 0x94000000), 25);
refuse('unknown instruction', word(integer, 180, 0xffffffff), 26);
refuse('memory instruction', word(integer, 180, 0xf9400000), 26);
refuse('Apple reserved X18', word(integer, 180, 0xd2800012), 26);
refuse('callee saved X19', word(integer, 180, 0xd2800013), 26);
refuse('SP immediate', word(integer, 180, 0x910003e0), 26);
refuse('indirect branch', word(integer, 180, 0xd61f0000), 26);
refuse('unknown syscall', word(integer, 180, 0xd4000001), 26);
refuse('body falls into startup', word(integer, 184, 0xd2800000), 26);
refuse('branch out of body', word(integer, 180, 0x14000002), 27);
refuse('conditional branch into bootstrap', word(integer, 180, 0x54000040), 27);
refuse('branch before body', word(integer, 180, 0x17ffffff), 27);
refuse('changed hello literal', word(hello, 180, 0x6c6c6548), 28);
refuse('changed hello syscall fd', word(hello, 260, 0xd2800040), 28);
refuse('changed hello syscall number', word(hello, 284, 0xd28007e8), 28);
refuse('changed hello address', word(hello, 264, 0xd28016a1), 28);
for (const [name, status] of [['memory-unsupported', 26], ['call-unsupported', 25], ['stdin-unsupported', 24]]) {
  const b = compile(name, fs.readFileSync(path.join(root, 'tests', `${name}.om`)));
  // Unsupported fixtures are real compiled programs, not fabricated backend results.
  refuse(`compiled ${name}`, b, status);
}
// All single-byte damage to exact hello outside the bootstrap/header must refuse.
test('hello exact-template byte closure (172 single-byte mutations)', () => {
  let count = 0;
  for (let p = 180; p < 352; p++) {
    // Mutating text/body must not reclassify into the leaf family.
    const b = Buffer.from(hello); b[p] ^= 1;
    const r = adapt(b); assert.equal(r.status, 28, `hello byte ${p}`); assert.equal(r.stdout.length, 0); count++;
  }
  save('hello-byte-closure.count', String(count) + '\n');
});
// Discriminating controls are compiled Omega adapters, not byte-patched executables.
const adapterSource = fs.readFileSync(path.join(root, 'adapter.om'), 'utf8');
const controls = [
  ['control-linux-exit', 'wr add 16384 sub p 176 3531604016', 'wr add 16384 sub p 176 3531606952', false],
  ['control-wrong-entry', 'wr 624 add 16384 sub e 176', 'wr 624 16384', false],
  ['control-wrong-error', 'wr add t 8 3665835008', 'wr add t 8 3573751839', true],
];
for (const [name, before, after, useHello] of controls) {
  assert.equal(adapterSource.split(before).length, 2, 'unique source mutation');
  const source = Buffer.from(adapterSource.replace(before, after)); save(`${name}.om`, source);
  compile(name, source); const exe = path.join(evidence, `${name}.elf`);
  test(`compiled ${name} caught by independent semantic oracle`, () => {
    const input = useHello ? hello : integer, r = adapt(input, exe);
    assert.equal(r.status, 0); assert.ok(r.stdout.length > 0);
    assert.equal(r.stdout.readUInt32LE(0), 0xfeedfacf); assert.equal(r.stdout.readUInt32LE(4), 0x100000c);
    save(`${name}.macho`, r.stdout); pin(`${name}.macho`, r.stdout);
    assert.throws(() => checkMacho(r.stdout, input, useHello), assert.AssertionError);
  });
}
pin('adapter.om', Buffer.from(adapterSource)); pin('adapter', fs.readFileSync(path.join(build, 'adapter')));
pins.push(`${compilerHash}  shared/omc-191b776e`);
save('SHA256SUMS', pins.join('\n') + '\n');
save('summary.json', JSON.stringify({ checks: results.length, compiledControls: controls.length,
  helloByteMutations: 172, linuxNative: fixtures.length, macOS: 'NOT_RUN', independentReview: 'NOT_RUN',
  compilerHash, checksPassed: results }, null, 2) + '\n');
console.log(`PASS ${results.length} checks; ${fixtures.length} Linux native fixtures; ${controls.length} compiled controls; macOS NOT_RUN`);
