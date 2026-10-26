#!/usr/bin/env node
// SPDX-License-Identifier: Apache-2.0
// Hand-written OSC IR v1/v7 byte writer and fixture set for the v7 oracle.
//
// Independent of seed/wasm/v7/oracle.js (no shared code: this file encodes,
// the oracle decodes) and of every producer/backend. Wire layout from
// spec/one-layer-format.md sections 3 and 10.
//
//   node writer.js OUTDIR      writes OUTDIR/<fixture>.unit for every fixture
//   require('./writer.js').encode(desc) -> Buffer
//
// desc = { version: 1|7, regions: [{ name, tag, cls, owner, count, init }],
//          funcs: [{ name, params: [tag..], ret: tag, regs: [tag..] (after the
//          params), blocks: [[insn..]..] }] }
// Tags may be names (u8 .. f64) or numbers. Register operand 'none' = 0xFFFF.
'use strict';

const fs = require('fs');
const path = require('path');

const TAG = { void: 0, bool: 1, u8: 2, u16: 3, u32: 4, u64: 5, i8: 6, i16: 7, i32: 8, i64: 9, f32: 13, f64: 14 };
const tagOf = (t) => (typeof t === 'number' ? t : TAG[t]);
const BINSUB = { add: 1, sub: 2, mul: 3, div: 4, rem: 5, and: 6, or: 7, xor: 8, shl: 9, shr: 10 };
const UNSUB = { neg: 1, bnot: 2, lnot: 3 };
const CMPSUB = { eq: 1, ne: 2, lt: 3, le: 4, gt: 5, ge: 6 };

class Bytes {
  constructor() { this.a = []; }
  u8(v) { if (v < 0 || v > 255) throw new Error('u8 ' + v); this.a.push(v); }
  u16(v) { this.u8(v & 0xff); this.u8((v >>> 8) & 0xff); }
  u32(v) { for (let k = 0; k < 4; k++) this.u8(Math.floor(v / 2 ** (8 * k)) % 256); }
  u64(v) { let x = BigInt.asUintN(64, BigInt(v)); for (let k = 0; k < 8; k++) { this.u8(Number(x & 0xffn)); x >>= 8n; } }
  raw(list) { for (const b of list) this.u8(b); }
  str(s, lenWidth) {
    const b = Buffer.from(s, 'latin1');
    if (lenWidth === 2) this.u16(b.length); else this.u8(b.length);
    this.raw(b);
  }
}

const regField = (v) => (v === 'none' ? 0xffff : v);

function insn(w, ins) {
  const [op, ...f] = ins;
  const r = (v) => w.u16(regField(v));
  switch (op) {
    case 'const': w.u8(1); r(f[0]); w.u64(f[1]); break;
    case 'mov': w.u8(2); r(f[0]); r(f[1]); break;
    case 'bin': w.u8(3); w.u8(typeof f[0] === 'number' ? f[0] : BINSUB[f[0]]); r(f[1]); r(f[2]); r(f[3]); break;
    case 'un': w.u8(4); w.u8(typeof f[0] === 'number' ? f[0] : UNSUB[f[0]]); r(f[1]); r(f[2]); break;
    case 'cmp': w.u8(5); w.u8(CMPSUB[f[0]]); r(f[1]); r(f[2]); r(f[3]); break;
    case 'cast': w.u8(6); r(f[0]); r(f[1]); break;
    case 'call': w.u8(11); r(f[0]); r(f[1]); w.u8(f.length - 2); f.slice(2).forEach(r); break;
    case 'trap': w.u8(12); w.u8(f[0]); break;
    case 'br': w.u8(13); r(f[0]); break;
    case 'cbr': w.u8(14); r(f[0]); r(f[1]); r(f[2]); break;
    case 'ret': w.u8(15); r(f[0]); break;
    case 'rload': w.u8(30); r(f[0]); r(f[1]); r(f[2]); break;
    case 'rstore': w.u8(31); r(f[0]); r(f[1]); r(f[2]); break;
    case 'bitcast': w.u8(32); r(f[0]); r(f[1]); break;
    case 'rinit': w.u8(33); r(f[0]); r(f[1]); break;
    case 'raw': w.raw(f); break; // opcode byte and fields verbatim
    default: throw new Error('writer: unknown insn ' + op);
  }
}

const TERMS = new Set(['br', 'cbr', 'ret']);

function func(w, fn, version) {
  w.str(fn.name, 1);
  const params = fn.params || [];
  w.u8(fn.nparams === undefined ? params.length : fn.nparams);
  w.u8(tagOf(fn.ret || 'void'));
  const regs = params.concat(fn.regs || []);
  w.u16(regs.length);
  regs.forEach((t) => w.u8(tagOf(t)));
  const blocks = fn.blocks;
  w.u16(blocks.length);
  let first = 0;
  for (const b of blocks) { w.u32(first); w.u32(b.length); first += b.length; }
  w.u32(first);
  for (const b of blocks) for (const i of b) insn(w, i);
  w.str(fn.requires || '', 2);
  w.str(fn.ensures || '', 2);
  void version;
}

function encode(desc) {
  const w = new Bytes();
  w.raw([0x4f, 0x53, 0x43, 0x31, 0x49, 0x52, 0x00]);
  w.u8(desc.version);
  if (desc.version !== 1) {
    w.u8(desc.nstructs || 0);
    const regions = desc.regions || [];
    w.u16(desc.nregions === undefined ? regions.length : desc.nregions);
    for (const g of regions) {
      w.str(g.name, 1);
      w.u8(tagOf(g.tag));
      w.u8(g.cls);
      w.u16(g.owner === undefined ? (g.cls === 1 ? 0 : 0xffff) : g.owner);
      w.u32(g.count);
      const init = g.init ? Buffer.from(g.init, 'latin1') : Buffer.alloc(0);
      w.u32(g.initlen === undefined ? init.length : g.initlen);
      w.raw(init);
    }
  }
  w.u16(desc.nfuncs === undefined ? desc.funcs.length : desc.nfuncs);
  for (const fn of desc.funcs) func(w, fn, desc.version);
  return Buffer.from(w.a);
}

// ------------------------------------------------------------- fixtures
// Register numbering in each function: params first, then `regs`.
const fx = {};
const mod = (name, tag, count) => ({ name, tag, cls: 0, owner: 0xffff, count });
const text = (name, s) => ({ name, tag: 'u8', cls: 2, owner: 0xffff, count: Buffer.from(s, 'latin1').length, init: s });
const loc = (name, tag, count, owner) => ({ name, tag, cls: 1, owner, count });

// set(i, v) -> void ; get(i) -> T over one module region 0 of element T.
function setget(tag, idx, count, extra) {
  return {
    version: 7,
    regions: [mod('g', tag, count)],
    funcs: [
      { name: 'set', params: [idx, tag], blocks: [[['rstore', 0, 0, 1], ['ret', 'none']]] },
      { name: 'get', params: [idx], ret: tag, regs: [tag], blocks: [[['rload', 1, 0, 0], ['ret', 1]]] },
    ].concat(extra || []),
  };
}

fx.rt_u8 = setget('u8', 'i32', 4);
fx.rt_i32 = setget('i32', 'u32', 3);
fx.rt_u64 = setget('u64', 'u64', 2, [
  { name: 'geti', params: ['i64'], ret: 'u64', regs: ['u64'], blocks: [[['rload', 1, 0, 0], ['ret', 1]]] },
]);
fx.rt_f32 = setget('f32', 'u8', 2);
fx.rt_f64 = setget('f64', 'i16', 2);

// Text read, including an empty text table.
fx.text_read = {
  version: 7,
  regions: [text('t', 'RGBX'), text('e', '')],
  funcs: [
    { name: 'tget', params: ['u32'], ret: 'u8', regs: ['u8'], blocks: [[['rload', 1, 0, 0], ['ret', 1]]] },
    { name: 'eget', params: ['u32'], ret: 'u8', regs: ['u8'], blocks: [[['rload', 1, 1, 0], ['ret', 1]]] },
  ],
};

// Local storage: initialized to 5 on every call, old value returned, then overwritten.
// reinit: RINIT 5, store 9, RINIT 7, load -> 7 (re-init resets the whole region).
fx.local_fresh = {
  version: 7,
  regions: [loc('l', 'u32', 4, 0), loc('l', 'u32', 2, 1)],
  funcs: [
    { name: 'swap', params: ['u32', 'u32'], ret: 'u32', regs: ['u32', 'u32'],
      blocks: [[['const', 2, 5], ['rinit', 0, 2], ['rload', 3, 0, 0], ['rstore', 0, 0, 1], ['ret', 3]]] },
    { name: 'reinit', params: ['u32'], ret: 'u32', regs: ['u32', 'u32', 'u32', 'u32'],
      blocks: [[['const', 1, 5], ['rinit', 1, 1], ['const', 2, 9], ['rstore', 1, 0, 2],
        ['const', 3, 7], ['rinit', 1, 3], ['rload', 4, 1, 0], ['ret', 4]]] },
  ],
};

// Module persistence across calls, a partial write before a bounds trap, and
// an overflow trap before any write.
fx.persist = {
  version: 7,
  regions: [mod('c', 'u32', 1)],
  funcs: [
    { name: 'bump', ret: 'u32', regs: ['u64', 'u32', 'u32', 'u32'],
      blocks: [[['const', 0, 0], ['rload', 1, 0, 0], ['const', 2, 1], ['bin', 'add', 3, 1, 2],
        ['rstore', 0, 0, 3], ['ret', 3]]] },
    { name: 'partial', ret: 'u32', regs: ['u64', 'u32', 'u32', 'u32', 'u64', 'u32'],
      blocks: [[['const', 0, 0], ['rload', 1, 0, 0], ['const', 2, 1], ['bin', 'add', 3, 1, 2],
        ['rstore', 0, 0, 3], ['const', 4, 1], ['rload', 5, 0, 4], ['ret', 5]]] },
    { name: 'ovf', params: ['i32'], ret: 'u32', regs: ['i32', 'u64', 'u32', 'u32'],
      blocks: [[['bin', 'add', 1, 0, 0], ['const', 2, 0], ['const', 3, 77], ['rstore', 0, 2, 3],
        ['ret', 3]]] },
  ],
};

// Bitcasts across each width set.
const bc = (name, from, to) => ({ name, params: [from], ret: to, regs: [to], blocks: [[['bitcast', 1, 0], ['ret', 1]]] });
fx.bitcast = {
  version: 7,
  funcs: [
    bc('f32_u32', 'f32', 'u32'), bc('u32_f32', 'u32', 'f32'), bc('i32_f32', 'i32', 'f32'),
    bc('f64_i64', 'f64', 'i64'), bc('u64_f64', 'u64', 'f64'), bc('u8_i8', 'u8', 'i8'),
    bc('i8_u8', 'i8', 'u8'), bc('i16_u16', 'i16', 'u16'), bc('u16_i16', 'u16', 'i16'),
    bc('f32_f32', 'f32', 'f32'),
  ],
};

// Conversions (CAST).
const cv = (name, from, to) => ({ name, params: [from], ret: to, regs: [to], blocks: [[['cast', 1, 0], ['ret', 1]]] });
fx.convert = {
  version: 7,
  funcs: [
    cv('f64_i32', 'f64', 'i32'), cv('f64_u8', 'f64', 'u8'), cv('f32_i8', 'f32', 'i8'),
    cv('f64_u64', 'f64', 'u64'), cv('f64_i64', 'f64', 'i64'), cv('f32_u32', 'f32', 'u32'),
    cv('f32_i16', 'f32', 'i16'),
    cv('u64_f32', 'u64', 'f32'), cv('i32_f32', 'i32', 'f32'), cv('u64_f64', 'u64', 'f64'),
    cv('i64_f64', 'i64', 'f64'), cv('f64_f32', 'f64', 'f32'), cv('f32_f64', 'f32', 'f64'),
    cv('f64_f64', 'f64', 'f64'), cv('i32_u8', 'i32', 'u8'), cv('u64_i64', 'u64', 'i64'),
  ],
};

// Float arithmetic, negation and comparison.
const fb = (name, t, sub) => ({ name, params: [t, t], ret: t, regs: [t], blocks: [[['bin', sub, 2, 0, 1], ['ret', 2]]] });
const fc = (name, t, sub) => ({ name, params: [t, t], ret: 'bool', regs: ['bool'], blocks: [[['cmp', sub, 2, 0, 1], ['ret', 2]]] });
fx.float_ops = {
  version: 7,
  funcs: [
    fb('fadd', 'f32', 'add'), fb('fmul', 'f32', 'mul'), fb('dsub', 'f64', 'sub'), fb('ddiv', 'f64', 'div'),
    { name: 'fneg', params: ['f32'], ret: 'f32', regs: ['f32'], blocks: [[['un', 'neg', 1, 0], ['ret', 1]]] },
    { name: 'dneg', params: ['f64'], ret: 'f64', regs: ['f64'], blocks: [[['un', 'neg', 1, 0], ['ret', 1]]] },
    fc('feq', 'f32', 'eq'), fc('fne', 'f32', 'ne'), fc('flt', 'f32', 'lt'), fc('dge', 'f64', 'ge'),
    { name: 'kf', ret: 'f32', regs: ['f32'], blocks: [[['const', 0, 0x7fa00001], ['ret', 0]]] },
    { name: 'kd', ret: 'f64', regs: ['f64'], blocks: [[['const', 0, 0xfff0000000000001n], ['ret', 0]]] },
  ],
};

// Initialization on both branches of a diamond, used after the join (accept).
fx.init_diamond = {
  version: 7,
  regions: [loc('d', 'i16', 3, 0)],
  funcs: [
    { name: 'pick', params: ['bool'], ret: 'i16', regs: ['i16', 'i16', 'u8', 'i16'],
      blocks: [
        [['cbr', 0, 1, 2]],
        [['const', 1, 0xfffffffffffffffdn], ['rinit', 0, 1], ['br', 3]],
        [['const', 2, 4], ['rinit', 0, 2], ['br', 3]],
        [['const', 3, 2], ['rload', 4, 0, 3], ['ret', 4]],
      ] },
  ],
};

// v1 units (no region, no float, no v7 opcode).
fx.v1_arith = {
  version: 1,
  funcs: [
    { name: 'addi', params: ['i32', 'i32'], ret: 'i32', regs: ['i32'], blocks: [[['bin', 'add', 2, 0, 1], ['ret', 2]]] },
    { name: 'addu', params: ['u8', 'u8'], ret: 'u8', regs: ['u8'], blocks: [[['bin', 'add', 2, 0, 1], ['ret', 2]]] },
    { name: 'divi', params: ['i64', 'i64'], ret: 'i64', regs: ['i64'], blocks: [[['bin', 'div', 2, 0, 1], ['ret', 2]]] },
    { name: 'remi', params: ['i64', 'i64'], ret: 'i64', regs: ['i64'], blocks: [[['bin', 'rem', 2, 0, 1], ['ret', 2]]] },
    { name: 'shl8', params: ['i8', 'u32'], ret: 'i8', regs: ['i8'], blocks: [[['bin', 'shl', 2, 0, 1], ['ret', 2]]] },
    { name: 'shr8', params: ['i8', 'u32'], ret: 'i8', regs: ['i8'], blocks: [[['bin', 'shr', 2, 0, 1], ['ret', 2]]] },
    { name: 'negu', params: ['u16'], ret: 'u16', regs: ['u16'], blocks: [[['un', 'neg', 1, 0], ['ret', 1]]] },
    { name: 'memory', params: ['bool'], ret: 'bool', regs: ['bool'], blocks: [[['un', 'lnot', 1, 0], ['ret', 1]]] },
    { name: 'stop', params: [], ret: 'void', blocks: [[['trap', 9], ['ret', 'none']]] },
  ],
};
// sum(n) = 0 + 1 + .. + (n-1), looping; twice(n) calls sum.
fx.v1_loop = {
  version: 1,
  funcs: [
    { name: 'sum', params: ['u32'], ret: 'u32', regs: ['u32', 'u32', 'bool', 'u32'],
      blocks: [
        [['const', 1, 0], ['const', 2, 0], ['br', 1]],
        [['cmp', 'lt', 3, 2, 0], ['cbr', 3, 2, 3]],
        [['bin', 'add', 1, 1, 2], ['const', 4, 1], ['bin', 'add', 2, 2, 4], ['br', 1]],
        [['ret', 1]],
      ] },
    { name: 'twice', params: ['u32'], ret: 'u32', regs: ['u32', 'u32'],
      blocks: [[['call', 1, 0, 0], ['bin', 'add', 2, 1, 1], ['ret', 2]]] },
  ],
};

// ----------------------------------------------------- malformed units
const tiny = (extra) => Object.assign({
  version: 7,
  regions: [mod('g', 'u32', 2)],
  funcs: [{ name: 'f', params: ['u32'], ret: 'u32', regs: ['u32'], blocks: [[['rload', 1, 0, 0], ['ret', 1]]] }],
}, extra || {});
const local1 = (body, regs, extra) => Object.assign({
  version: 7,
  regions: [loc('l', 'u32', 2, 0)],
  funcs: [{ name: 'f', params: ['u32'], ret: 'u32', regs: regs || ['u32', 'u32'], blocks: body }],
}, extra || {});

// Reader classes.
const bytesOf = {};
bytesOf.bad_magic = () => { const b = encode(tiny()); b[3] = 0x32; return b; };
bytesOf.bad_version2 = () => { const b = encode(tiny()); b[7] = 2; return b; };
bytesOf.bad_version6 = () => { const b = encode(tiny()); b[7] = 6; return b; };
bytesOf.bad_version8 = () => { const b = encode(tiny()); b[7] = 8; return b; };
bytesOf.trunc_empty = () => Buffer.alloc(0);
bytesOf.trunc_magic = () => encode(tiny()).subarray(0, 4);
bytesOf.trunc_header = () => encode(tiny()).subarray(0, 8);
bytesOf.trunc_last = () => { const b = encode(tiny()); return b.subarray(0, b.length - 1); };
bytesOf.trunc_initdata = () => encode({ version: 7, regions: [text('t', 'RGBX')], funcs: [] }).subarray(0, 8 + 1 + 2 + 2 + 1 + 1 + 2 + 4 + 4 + 2);
bytesOf.trailing = () => Buffer.concat([encode(tiny()), Buffer.from([0])]);
bytesOf.cap_v7_input = () => { const b = Buffer.alloc(8388609); encode(tiny()).copy(b); return b; };
bytesOf.cap_v1_input = () => { const b = Buffer.alloc(2620252); encode(fx.v1_arith).copy(b); return b; };
bytesOf.v7_input_at_v1_cap = () => {
  // A v7 header over the v1 cap is not a capacity refusal; it fails later as TRAILING.
  const b = Buffer.alloc(2620252); encode(tiny()).copy(b); return b;
};

fx.cap_nfuncs = { version: 7, regions: [mod('g', 'u8', 1)], nfuncs: 33, funcs: [] };
fx.cap_fname = tiny({ funcs: [{ name: 'x'.repeat(64), ret: 'void', blocks: [[['ret', 'none']]] }] });
fx.cap_nstructs = tiny({ nstructs: 17 });
fx.unsup_structs = tiny({ nstructs: 1 });
fx.unsup_class3 = tiny({ regions: [{ name: 'w', tag: 'u32', cls: 3, owner: 0xffff, count: 4 }] });
fx.unsup_op7 = tiny({ funcs: [{ name: 'f', params: ['u32'], ret: 'u32', regs: ['u32'], blocks: [[['raw', 7, 1, 0, 0, 0], ['ret', 0]]] }] });
fx.unsup_op20 = tiny({ funcs: [{ name: 'f', ret: 'void', blocks: [[['raw', 20, 0, 0], ['ret', 'none']]] }] });
fx.unsup_op34 = tiny({ funcs: [{ name: 'f', ret: 'void', blocks: [[['raw', 34], ['ret', 'none']]] }] });
fx.unsup_op0 = tiny({ funcs: [{ name: 'f', ret: 'void', blocks: [[['raw', 0], ['ret', 'none']]] }] });
fx.unsup_op9 = tiny({ funcs: [{ name: 'f', params: ['u32'], ret: 'u32', regs: ['u32'], blocks: [[['raw', 9, 1, 0, 0, 0, 0, 0], ['ret', 1]]] }] });
fx.v1_unsup_op10 = { version: 1, funcs: [{ name: 'f', params: ['u32'], ret: 'void', blocks: [[['raw', 10, 0, 0, 0, 0, 0, 0], ['ret', 'none']]] }] };
fx.unsup_tag10 = tiny({ funcs: [{ name: 'f', ret: 'void', regs: [10], blocks: [[['ret', 'none']]] }] });
fx.unsup_tag15 = tiny({ funcs: [{ name: 'f', ret: 'void', regs: [15], blocks: [[['ret', 'none']]] }] });
fx.v1_op30 = { version: 1, funcs: [{ name: 'f', params: ['u32'], ret: 'u32', regs: ['u32'], blocks: [[['raw', 30, 1, 0, 0, 0, 0, 0], ['ret', 1]]] }] };
fx.v1_tag13 = { version: 1, funcs: [{ name: 'f', params: ['f32'], ret: 'void', blocks: [[['ret', 'none']]] }] };
fx.v1_tag14_ret = { version: 1, funcs: [{ name: 'f', ret: 'f64', regs: ['f64'], blocks: [[['const', 0, 0], ['ret', 0]]] }] };

// Region table INVALIDs.
const oneRegion = (g) => tiny({ regions: [g] });
fx.inv_class4 = oneRegion({ name: 'g', tag: 'u32', cls: 4, owner: 0xffff, count: 2 });
fx.inv_rtag_bool = oneRegion({ name: 'g', tag: 'bool', cls: 0, owner: 0xffff, count: 2 });
fx.inv_rtag_cells = oneRegion({ name: 'g', tag: 12, cls: 0, owner: 0xffff, count: 2 });
fx.inv_rname_empty = oneRegion({ name: '', tag: 'u32', cls: 0, owner: 0xffff, count: 2 });
fx.inv_rname_dot = oneRegion({ name: 'a.b', tag: 'u32', cls: 0, owner: 0xffff, count: 2 });
fx.inv_rname_nul = oneRegion({ name: 'a\u0000', tag: 'u32', cls: 0, owner: 0xffff, count: 2 });
fx.inv_rname_8bit = oneRegion({ name: 'g\u00e9', tag: 'u32', cls: 0, owner: 0xffff, count: 2 });
fx.inv_rname_64 = oneRegion({ name: 'r'.repeat(64), tag: 'u32', cls: 0, owner: 0xffff, count: 2 });
fx.inv_mod_init = oneRegion({ name: 'g', tag: 'u32', cls: 0, owner: 0xffff, count: 1, init: 'abcd' });
fx.inv_text_len = oneRegion({ name: 'g', tag: 'u8', cls: 2, owner: 0xffff, count: 5, init: 'RGBX' });
fx.inv_text_u32 = oneRegion({ name: 'g', tag: 'u32', cls: 2, owner: 0xffff, count: 4, init: 'RGBX' });
fx.inv_count0 = oneRegion({ name: 'g', tag: 'u32', cls: 0, owner: 0xffff, count: 0 });
fx.inv_count_big = oneRegion({ name: 'g', tag: 'u8', cls: 0, owner: 0xffff, count: 16777217 });
fx.ok_count_max = tiny({ regions: [mod('g', 'u32', 2), mod('h', 'u8', 16777216)] });
fx.inv_nregions = tiny({ nregions: 1025, regions: [] });
fx.inv_owner_local_module = tiny({ regions: [mod('g', 'u32', 2), loc('l', 'u32', 2, 0xffff)] });
fx.inv_owner_missing = tiny({ regions: [mod('g', 'u32', 2), loc('l', 'u32', 2, 1)] });
fx.inv_owner_module0 = oneRegion({ name: 'g', tag: 'u32', cls: 0, owner: 0, count: 2 });
fx.inv_dup_module = tiny({ regions: [mod('g', 'u32', 2), text('g', 'xy')] });
fx.inv_shadow = tiny({ regions: [mod('g', 'u32', 2), loc('g', 'u32', 2, 0)] });
fx.inv_dup_local = tiny({ regions: [mod('g', 'u32', 2), loc('l', 'u8', 1, 0), loc('l', 'u8', 1, 0)] });

// Export collisions (v7 reserves memory and region.NAME); duplicate functions.
const named = (n) => ({ name: n, ret: 'void', blocks: [[['ret', 'none']]] });
fx.inv_export_memory = tiny({ funcs: tiny().funcs.concat([named('memory')]) });
fx.inv_export_region = tiny({ funcs: tiny().funcs.concat([named('region.g')]) });
fx.inv_dup_func = tiny({ funcs: tiny().funcs.concat([named('f')]) });
fx.inv_v1_dup_func = { version: 1, funcs: [named('f'), named('f')] };
fx.ok_fname_empty = tiny({ funcs: tiny().funcs.concat([named('')]) });
fx.inv_fname_8bit = tiny({ funcs: tiny().funcs.concat([named('h\u00e9')]) });

// Local initialization controls.
fx.inv_missing_init = local1([[['const', 1, 0], ['rload', 2, 0, 1], ['ret', 2]]]);
fx.inv_init_after_use = local1([[['const', 1, 0], ['rload', 2, 0, 1], ['rinit', 0, 0], ['ret', 2]]]);
fx.inv_store_before_init = local1([[['const', 1, 0], ['rstore', 0, 1, 0], ['rinit', 0, 0], ['ret', 0]]]);
fx.inv_init_one_branch = local1([
  [['const', 1, 0], ['cmp', 'eq', 3, 0, 1], ['cbr', 3, 1, 2]],
  [['rinit', 0, 0], ['br', 2]],
  [['rload', 2, 0, 1], ['ret', 2]],
], ['u32', 'u32', 'bool']);
// Header 1 loads; RINIT happens only in body 2, which loops back to 1.
fx.inv_loop_carried = local1([
  [['const', 1, 0], ['br', 1]],
  [['rload', 2, 0, 1], ['cmp', 'eq', 3, 2, 0], ['cbr', 3, 3, 2]],
  [['rinit', 0, 0], ['br', 1]],
  [['ret', 2]],
], ['u32', 'u32', 'bool']);
fx.ok_loop_init_before = local1([
  [['const', 1, 0], ['rinit', 0, 0], ['br', 1]],
  [['rload', 2, 0, 1], ['cmp', 'eq', 3, 2, 0], ['cbr', 3, 3, 2]],
  [['rinit', 0, 1], ['br', 1]],
  [['ret', 2]],
], ['u32', 'u32', 'bool']);
fx.inv_unreachable_use = local1([
  [['rinit', 0, 0], ['const', 1, 0], ['rload', 2, 0, 1], ['ret', 2]],
  [['const', 1, 0], ['rload', 2, 0, 1], ['ret', 2]],
]);
fx.inv_reg_undef = local1([[['rinit', 0, 0], ['rload', 2, 0, 1], ['ret', 2]]]);

// Ownership.
const twoFuncLocal = (body1) => ({
  version: 7,
  regions: [loc('l', 'u32', 2, 0), mod('g', 'u32', 2)],
  funcs: [
    { name: 'a', params: ['u32'], ret: 'u32', regs: ['u32', 'u32'],
      blocks: [[['rinit', 0, 0], ['const', 1, 0], ['rload', 2, 0, 1], ['ret', 2]]] },
    { name: 'b', params: ['u32'], ret: 'u32', regs: ['u32', 'u32'], blocks: [body1] },
  ],
});
fx.ok_own_local = twoFuncLocal([['rload', 2, 1, 0], ['ret', 2]]);
fx.inv_foreign_load = twoFuncLocal([['const', 1, 0], ['rload', 2, 0, 1], ['ret', 2]]);
fx.inv_foreign_init = twoFuncLocal([['rinit', 0, 0], ['ret', 0]]);
fx.inv_foreign_store = twoFuncLocal([['const', 1, 0], ['rstore', 0, 1, 0], ['ret', 0]]);
fx.inv_rinit_module = twoFuncLocal([['rinit', 1, 0], ['ret', 0]]);

// Read-only text.
const textUnit = (body, regs) => ({
  version: 7,
  regions: [text('t', 'RGBX')],
  funcs: [{ name: 'f', params: ['u8'], ret: 'u8', regs: regs || ['u32'], blocks: [body] }],
});
fx.inv_text_store = textUnit([['const', 1, 0], ['rstore', 0, 1, 0], ['ret', 0]]);
fx.inv_text_init = textUnit([['rinit', 0, 0], ['ret', 0]]);

// Operands: sentinel / out of table.
fx.inv_rload_dst_none = tiny({ funcs: [{ name: 'f', params: ['u32'], ret: 'u32', blocks: [[['rload', 'none', 0, 0], ['ret', 0]]] }] });
fx.inv_rload_region_none = tiny({ funcs: [{ name: 'f', params: ['u32'], ret: 'u32', regs: ['u32'], blocks: [[['rload', 1, 'none', 0], ['ret', 1]]] }] });
fx.inv_rload_index_none = tiny({ funcs: [{ name: 'f', params: ['u32'], ret: 'u32', regs: ['u32'], blocks: [[['rload', 1, 0, 'none'], ['ret', 1]]] }] });
fx.inv_rload_region_oob = tiny({ funcs: [{ name: 'f', params: ['u32'], ret: 'u32', regs: ['u32'], blocks: [[['rload', 1, 1, 0], ['ret', 1]]] }] });
fx.inv_rload_index_8000 = tiny({ funcs: [{ name: 'f', params: ['u32'], ret: 'u32', regs: ['u32'], blocks: [[['rload', 1, 0, 0x8000], ['ret', 1]]] }] });
fx.inv_rstore_src_fffe = tiny({ funcs: [{ name: 'f', params: ['u32'], ret: 'u32', blocks: [[['rstore', 0, 0, 0xfffe], ['ret', 0]]] }] });
fx.inv_rstore_src_oob = tiny({ funcs: [{ name: 'f', params: ['u32'], ret: 'u32', blocks: [[['rstore', 0, 0, 1], ['ret', 0]]] }] });
fx.inv_bitcast_none = tiny({ funcs: [{ name: 'f', params: ['u32'], ret: 'i32', regs: ['i32'], blocks: [[['bitcast', 1, 'none'], ['ret', 1]]] }] });
fx.inv_rinit_none = local1([[['rinit', 0, 'none'], ['ret', 0]]]);
fx.inv_rinit_region_none = local1([[['rinit', 'none', 0], ['ret', 0]]]);

// Typing.
fx.inv_index_float = tiny({ funcs: [{ name: 'f', params: ['f32'], ret: 'u32', regs: ['u32'], blocks: [[['rload', 1, 0, 0], ['ret', 1]]] }] });
fx.inv_index_bool = tiny({ funcs: [{ name: 'f', params: ['bool'], ret: 'u32', regs: ['u32'], blocks: [[['rload', 1, 0, 0], ['ret', 1]]] }] });
fx.inv_rload_type = tiny({ funcs: [{ name: 'f', params: ['u32'], ret: 'i32', regs: ['i32'], blocks: [[['rload', 1, 0, 0], ['ret', 1]]] }] });
fx.inv_rstore_type = tiny({ funcs: [{ name: 'f', params: ['u32', 'u64'], ret: 'void', blocks: [[['rstore', 0, 0, 1], ['ret', 'none']]] }] });
fx.inv_rinit_type = local1([[['const', 1, 0], ['cast', 2, 1], ['rinit', 0, 2], ['ret', 0]]], ['u32', 'i32']);
fx.inv_bitcast_cross = { version: 7, funcs: [bc('f', 'u32', 'f64')] };
fx.inv_bitcast_bool = { version: 7, funcs: [bc('f', 'bool', 'u8'), bc('g', 'f32', 'f32')] };
fx.inv_bitcast_width = { version: 7, funcs: [bc('f', 'u8', 'u16'), bc('g', 'f32', 'f32')] };
fx.inv_float_rem = { version: 7, funcs: [fb('f', 'f32', 'rem')] };
fx.inv_float_shl = { version: 7, funcs: [fb('f', 'f64', 'shl')] };
fx.inv_float_and = { version: 7, funcs: [fb('f', 'f64', 'and')] };
fx.inv_float_bnot = { version: 7, funcs: [{ name: 'f', params: ['f32'], ret: 'f32', regs: ['f32'], blocks: [[['un', 'bnot', 1, 0], ['ret', 1]]] }] };
fx.inv_float_lnot = { version: 7, funcs: [{ name: 'f', params: ['f64'], ret: 'f64', regs: ['f64'], blocks: [[['un', 'lnot', 1, 0], ['ret', 1]]] }] };
fx.inv_f32_const_upper = { version: 7, funcs: [{ name: 'f', ret: 'f32', regs: ['f32'], blocks: [[['const', 0, 0x100000000n], ['ret', 0]]] }] };
fx.inv_cast_bool = { version: 7, funcs: [cv('f', 'bool', 'f32')] };
fx.inv_cast_to_bool = { version: 7, funcs: [cv('f', 'f64', 'bool')] };

// Version selection.
fx.gratuitous_v7 = { version: 7, funcs: fx.v1_loop.funcs };
fx.gratuitous_v7_empty = { version: 7, funcs: [] };

function writeAll(dir) {
  fs.mkdirSync(dir, { recursive: true });
  const names = [];
  for (const [n, d] of Object.entries(fx)) { fs.writeFileSync(path.join(dir, n + '.unit'), encode(d)); names.push(n); }
  for (const [n, f] of Object.entries(bytesOf)) { fs.writeFileSync(path.join(dir, n + '.unit'), f()); names.push(n); }
  return names;
}

module.exports = { encode, fixtures: fx, rawFixtures: bytesOf, writeAll };

if (require.main === module) {
  if (process.argv.length !== 3) {
    process.stderr.write('usage: writer.js OUTDIR\n');
    process.exit(2);
  }
  const names = writeAll(process.argv[2]);
  process.stdout.write(names.length + ' fixtures\n');
}
