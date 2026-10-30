// SPDX-License-Identifier: Apache-2.0
// Read-only host executor for an irwasm v7 module. It never generates code:
// it instantiates the module irwasm emitted, runs a script and prints the
// same lines as `seed/wasm/v7/oracle.js run`, so the two outputs diff empty.
//
// usage: node seed/wasm/v7run.js MODULE.wasm SCRIPT
// Script lines: `call NAME ARG...` and `region NAME`; blank and `#` lines skip.
// Integer arguments are decimal; f32/f64 arguments are 0x + 8/16 hex digits.
// Output: `NAME = V`, `NAME TRAP`, `region NAME <hex>`.
//
// The OSC tags of every parameter, result and module region come from the
// custom section `osc.v7.abi` that irwasm appends to every v7 module; wasm
// types alone cannot say whether an i32 is u8 or i32.
// Exit: 0 done, 2 usage/script error, 3 non-trap engine error.
'use strict';

const fs = require('fs');

const T_VOID = 0, T_BOOL = 1, T_F32 = 13, T_F64 = 14;
const WIDTH = { 1: 1, 2: 8, 3: 16, 4: 32, 5: 64, 6: 8, 7: 16, 8: 32, 9: 64, 13: 32, 14: 64 };
const signed = (t) => t >= 6 && t <= 9;
const isFloat = (t) => t === T_F32 || t === T_F64;

function usage(msg) {
  process.stderr.write('v7run: ' + msg + '\nusage: v7run.js MODULE.wasm SCRIPT\n');
  process.exit(2);
}

function readFile(p) {
  try { return fs.readFileSync(p); } catch (e) { usage('cannot read ' + p + ': ' + e.code); }
}

class Reader {
  constructor(buf, pos, end) { this.buf = buf; this.pos = pos; this.end = end; }
  byte() {
    if (this.pos >= this.end) usage('truncated osc.v7.abi section');
    return this.buf[this.pos++];
  }
  leb() {
    let v = 0, shift = 0;
    for (;;) {
      const b = this.byte();
      v += (b & 127) * 2 ** shift;
      if (b < 128) return v;
      shift += 7;
    }
  }
  name() {
    const n = this.leb();
    if (this.pos + n > this.end) usage('truncated name');
    const s = this.buf.subarray(this.pos, this.pos + n).toString('latin1');
    this.pos += n;
    return s;
  }
}

// Find the custom section and decode function signatures and module regions.
function readAbi(buf) {
  const top = new Reader(buf, 8, buf.length);
  while (top.pos < buf.length) {
    const id = top.byte();
    const size = top.leb();
    const end = top.pos + size;
    if (id === 0) {
      const r = new Reader(buf, top.pos, end);
      if (r.name() === 'osc.v7.abi') {
        const funcs = [];
        for (let n = r.leb(), i = 0; i < n; i++) {
          const name = r.name();
          const params = [];
          for (let k = r.leb(), j = 0; j < k; j++) params.push(r.leb());
          funcs.push({ name, params, ret: r.leb() });
        }
        const regions = [];
        for (let n = r.leb(), i = 0; i < n; i++) {
          regions.push({ name: r.name(), tag: r.leb(), cls: r.leb(), count: r.leb() });
        }
        return { funcs, regions };
      }
    }
    top.pos = end;
  }
  usage('module has no osc.v7.abi section (not an irwasm v7 module)');
}

const dv = new DataView(new ArrayBuffer(8));

function parseArg(t, s) {
  if (isFloat(t)) {
    const digits = t === T_F32 ? 8 : 16;
    if (!new RegExp('^0x[0-9a-fA-F]{' + digits + '}$').test(s)) return null;
    const bits = BigInt(s);
    if (t === T_F32) { dv.setUint32(0, Number(bits)); return dv.getFloat32(0); }
    dv.setBigUint64(0, bits); return dv.getFloat64(0);
  }
  if (!/^-?[0-9]+$/.test(s)) return null;
  const v = BigInt(s);
  const w = BigInt(WIDTH[t]);
  const lo = t === T_BOOL ? 0n : signed(t) ? -(1n << (w - 1n)) : 0n;
  const hi = t === T_BOOL ? 1n : signed(t) ? (1n << (w - 1n)) - 1n : (1n << w) - 1n;
  if (v < lo || v > hi) return null;
  if (WIDTH[t] === 64) return BigInt.asIntN(64, v);
  return Number(BigInt.asIntN(32, v));
}

function formatValue(t, r) {
  if (t === T_VOID) return 'void';
  if (t === T_F32) { dv.setFloat32(0, r); return '0x' + dv.getUint32(0).toString(16).padStart(8, '0'); }
  if (t === T_F64) { dv.setFloat64(0, r); return '0x' + dv.getBigUint64(0).toString(16).padStart(16, '0'); }
  const w = WIDTH[t];
  const v = BigInt(r);
  return (signed(t) ? BigInt.asIntN(w, v) : BigInt.asUintN(w, v)).toString(10);
}

function main(argv) {
  if (argv.length !== 2) usage('bad arguments');
  const bytes = readFile(argv[0]);
  const abi = readAbi(bytes);
  let inst;
  try {
    inst = new WebAssembly.Instance(new WebAssembly.Module(bytes), {});
  } catch (e) {
    usage('module rejected: ' + e.message);
  }
  const ex = inst.exports;
  const script = readFile(argv[1]).toString('utf8').split('\n');
  const out = [];
  const flush = () => { if (out.length) process.stdout.write(out.join('')); out.length = 0; };
  for (let ln = 0; ln < script.length; ln++) {
    const line = script[ln].trim();
    if (line === '' || line.startsWith('#')) continue;
    const w = line.split(/\s+/);
    const bad = (m) => { flush(); usage('script line ' + (ln + 1) + ': ' + m); };
    if (w[0] === 'call') {
      const f = abi.funcs.find((g) => g.name === w[1]);
      if (!f) bad('no function ' + w[1]);
      if (w.length - 2 !== f.params.length) bad('expected ' + f.params.length + ' arguments');
      const vals = w.slice(2).map((s, k) => {
        const v = parseArg(f.params[k], s);
        if (v === null) bad('argument ' + s + ' does not fit tag ' + f.params[k]);
        return v;
      });
      try {
        out.push(f.name + ' = ' + formatValue(f.ret, ex[f.name](...vals)) + '\n');
      } catch (e) {
        if (!(e instanceof WebAssembly.RuntimeError)) {
          flush(); process.stderr.write('v7run: ' + e.message + '\n'); return 3;
        }
        out.push(f.name + ' TRAP\n');
      }
    } else if (w[0] === 'region' && w.length === 2) {
      const rg = abi.regions.find((g) => g.name === w[1]);
      if (!rg) bad('no module region ' + w[1]);
      const base = ex['region.' + rg.name].value >>> 0;
      const len = rg.count * (WIDTH[rg.tag] / 8);
      const mem = new Uint8Array(ex.memory.buffer, base, len);
      out.push('region ' + rg.name + ' ' + Buffer.from(mem).toString('hex') + '\n');
    } else bad('unknown command ' + JSON.stringify(line));
  }
  flush();
  return 0;
}

process.exitCode = main(process.argv.slice(2));
