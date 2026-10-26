// SPDX-License-Identifier: Apache-2.0
// Verification oracle only: reads artifacts; never generates IR, wasm, or native code.
// Usage: node compare-glow.js glow.wasm glow-cases.tsv oracle.bin
// Replays every case through the module's scalar exports and compares the full
// canvas of every frame, byte for byte, with the pinned C oracle's output.
// Pixels are read back through the scalar export fbget, so this check does not
// depend on where the memory backend places fb; a direct linear-memory view is
// for the parent to add once the memory ABI is agreed.
// Exit 0 identical, 1 pixel or length mismatch, 2 usage or module refusal.
'use strict';
const fs = require('node:fs');
const [wasmPath, table, oraclePath] = process.argv.slice(2);
if (!oraclePath) { console.error('usage: compare-glow.js glow.wasm glow-cases.tsv oracle.bin'); process.exit(2); }
const bytes = fs.readFileSync(wasmPath);
if (!WebAssembly.validate(bytes)) { console.error('invalid wasm'); process.exit(2); }
const module = new WebAssembly.Module(bytes);
if (WebAssembly.Module.imports(module).length) { console.error('unexpected import'); process.exit(2); }
const ex = new WebAssembly.Instance(module).exports;
for (const name of ['fbsize', 'fbclip', 'fbgrad', 'ckglow', 'fbget']) {
  if (typeof ex[name] !== 'function') { console.error(`missing export ${name}`); process.exit(2); }
}
const oracle = fs.readFileSync(oraclePath);
const frames = [];
let frame = null;
const dump = () => {
  if (!frame) return;
  const out = Buffer.alloc(frame.w * frame.h * 4);
  for (let i = 0; i < frame.w * frame.h; i++) out.writeUInt32LE(ex.fbget(i) >>> 0, i * 4);
  frames.push({ name: frame.name, bytes: out });
};
for (const line of fs.readFileSync(table, 'utf8').split('\n')) {
  if (!line || line.startsWith('#')) continue;
  const [op, ...f] = line.split('\t');
  const n = f.map(Number);
  if (op === 'frame') { dump(); frame = { name: f[0], w: Number(f[1]), h: Number(f[2]) }; ex.fbsize(frame.w, frame.h); }
  else if (!frame) { console.error('case before frame'); process.exit(2); }
  else if (op === 'clip') ex.fbclip(n[0], n[1], n[2], n[3]);
  else if (op === 'grad') ex.fbgrad();
  else if (op === 'glow') ex.ckglow(n[0], n[1], n[2], ((n[3] << 16) | (n[4] << 8) | n[5]) >>> 0, n[6] >>> 0);
  else { console.error(`unknown case ${op}`); process.exit(2); }
}
dump();
const all = Buffer.concat(frames.map(f => f.bytes));
if (all.length !== oracle.length) {
  console.error(`length mismatch wasm=${all.length} oracle=${oracle.length}`);
  process.exit(1);
}
let at = 0;
for (const f of frames) {
  const want = oracle.subarray(at, at + f.bytes.length);
  if (!want.equals(f.bytes)) {
    for (let i = 0; i < f.bytes.length; i += 4) {
      if (want.readUInt32LE(i) !== f.bytes.readUInt32LE(i)) {
        console.error(`pixel mismatch frame=${f.name} index=${i / 4} wasm=${f.bytes.readUInt32LE(i)} oracle=${want.readUInt32LE(i)}`);
        break;
      }
    }
    process.exit(1);
  }
  at += f.bytes.length;
}
console.log(`PASS glow wasm frames=${frames.length} bytes=${all.length} identical to pinned C oracle`);
