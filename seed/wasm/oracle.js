// SPDX-License-Identifier: Apache-2.0
// Verification oracle only: reads artifacts; never generates IR, wasm, or native code.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const [dir, table] = process.argv.slice(2);
const rows = fs.readFileSync(table, 'utf8').split('\n')
  .filter(line => line && !line.startsWith('#')).map(line => line.split('\t'));
const modules = new Map();
const covered = new Map();
let traps = 0;
for (const [index, [unit, name, ret, typed, status]] of rows.entries()) {
  if (!modules.has(unit)) {
    const file = unit === 'mutant' && process.argv[4] === '--mutant-baseline'
      ? 'mutant-flipped.wasm' : `${unit}.wasm`;
    const bytes = fs.readFileSync(path.join(dir, file));
    assert(WebAssembly.validate(bytes), `${unit}: invalid wasm`);
    const module = new WebAssembly.Module(bytes);
    assert.deepEqual(WebAssembly.Module.imports(module), [], `${unit}: unexpected import`);
    modules.set(unit, new WebAssembly.Instance(module).exports);
    covered.set(unit, new Set());
  }
  const exports = modules.get(unit);
  assert.equal(typeof exports[name], 'function', `${unit}: missing export ${name}`);
  covered.get(unit).add(name);
  const args = typed === '-' ? [] : typed.split(',').map(arg => {
    const [type, value] = arg.split(':');
    if (type === 'bool') return value === 'true' ? 1 : 0;
    return type === 'ulong' || type === 'long' ? BigInt(value) : Number(value);
  });
  const nativeStatus = Number(fs.readFileSync(path.join(dir, `case-${index + 1}.status`), 'utf8'));
  assert.equal(nativeStatus, Number(status), `${unit}.${name}: unexpected native status`);
  const native = fs.readFileSync(path.join(dir, `case-${index + 1}.out`));
  let result;
  let error;
  try { result = exports[name](...args); } catch (caught) { error = caught; }
  if (nativeStatus) {
    assert(nativeStatus >= 101 && nativeStatus <= 114, 'native status is not an Omega trap');
    assert(error instanceof WebAssembly.RuntimeError, `${unit}.${name}: missing wasm trap`);
    assert.equal(native.length, 0, `${unit}.${name}: native trap wrote a value`);
    traps++;
  } else {
    assert.equal(error, undefined, `${unit}.${name}: unexpected wasm trap ${error}`);
    assert.equal(native.length, 8, `${unit}.${name}: incomplete native result`);
    if (ret === 'void') result = 0n;
    else if (ret === 'uint') result = BigInt(result >>> 0);
    else result = BigInt(result);
    assert.equal(BigInt.asUintN(64, result), native.readBigUInt64LE(), `${unit}.${name}: result mismatch at case ${index + 1}`);
  }
}
for (const [unit, exports] of modules) {
  assert.deepEqual([...covered.get(unit)].sort(), Object.keys(exports).sort(), `${unit}: untested export`);
}
const mutant = fs.readFileSync(path.join(dir, 'mutant-flipped.wasm'));
assert(WebAssembly.validate(mutant), 'mutant must remain valid wasm');
const changed = new WebAssembly.Instance(new WebAssembly.Module(mutant)).exports.mutadd(19, 7);
assert.notEqual(changed, modules.get('mutant').mutadd(19, 7), 'add-to-sub mutant escaped');
console.log(`PASS parity cases=${rows.length} units=${modules.size} traps=${traps}; every export exercised`);
console.log('PASS add-to-sub IR-byte mutant changes executed result');
