#!/usr/bin/env node
// SPDX-License-Identifier: Apache-2.0
// Independent read-only oracle for OSC IR wire versions 1 and 7.
//
// Written from spec/one-layer-format.md sections 3..7 and 10 and
// docs/one_layer/OMEGESE.md 11.1/11.2 only. It shares no code with
// seed/wasm/irwasm.om, seed/wasm/oracle.js, seed/omc/omc.om, the C IR tools,
// or seed/wasm/v7/writer.js (the fixture writer). It is a referee: it never
// produces IR or wasm.
//
//   node oracle.js check UNIT            -> ACCEPT v1|v7 / REFUSE CLASS detail
//   node oracle.js run [--codes] UNIT SCRIPT
//
// Exit: check 0 accept, 1 refuse, 2 usage. run 0 done, 1 unit refused,
// 2 usage/script error, 3 oracle resource limit (not an Omega result).
'use strict';

const fs = require('fs');

const MAGIC = [0x4f, 0x53, 0x43, 0x31, 0x49, 0x52, 0x00];
const V1_INPUT_MAX = 2620251;
const V7_INPUT_MAX = 8388608;
const MAX_FUNCS = 32;
const MAX_PARAMS = 6;
const MAX_VREGS = 480;
const MAX_BLOCKS = 512;
const MAX_INSNS = 4096;
const MAX_STRUCTS = 16;
const MAX_REGIONS = 1024;
const MAX_COUNT = 16777216;
const NAME_MAX = 63;
const CLAUSE_MAX = 255;
const NONE = 0xffff;
const STEP_LIMIT = 200000000;

// Scalar wire tags.
const T_VOID = 0, T_BOOL = 1, T_F32 = 13, T_F64 = 14;
const isInt = (t) => t >= 2 && t <= 9;
const isFloat = (t) => t === T_F32 || t === T_F64;
const isValue = (t) => t === T_BOOL || isInt(t) || isFloat(t);
const isSigned = (t) => t >= 6 && t <= 9;
const intWidth = (t) => [0, 1, 8, 16, 32, 64, 8, 16, 32, 64][t];
const elemBytes = (t) => (t === T_F32 ? 4 : t === T_F64 ? 8 : intWidth(t) / 8);
const BITCAST_SET = { 2: 0, 6: 0, 3: 1, 7: 1, 4: 2, 8: 2, 13: 2, 5: 3, 9: 3, 14: 3 };

// Opcodes.
const OP = {
  CONST: 1, MOV: 2, BIN: 3, UN: 4, CMP: 5, CAST: 6, LOAD: 9, STORE: 10,
  CALL: 11, TRAP: 12, BR: 13, CBR: 14, RET: 15,
  RLOAD: 30, RSTORE: 31, BITCAST: 32, RINIT: 33,
};
const isTerm = (op) => op === OP.BR || op === OP.CBR || op === OP.RET;

class Refusal extends Error {
  constructor(cls, detail) {
    super(cls + ' ' + detail);
    this.cls = cls;
    this.detail = detail;
  }
}
const refuse = (cls, detail) => { throw new Refusal(cls, detail); };

// ---------------------------------------------------------------- reader
class Reader {
  constructor(buf) { this.buf = buf; this.pos = 0; }
  need(n, what) {
    if (this.pos + n > this.buf.length) refuse('TRUNCATED', what + ' at byte ' + this.pos);
  }
  u8(what) { this.need(1, what); return this.buf[this.pos++]; }
  u16(what) { this.need(2, what); const v = this.buf[this.pos] | (this.buf[this.pos + 1] << 8); this.pos += 2; return v; }
  u32(what) { this.need(4, what); const v = this.buf.readUInt32LE(this.pos); this.pos += 4; return v; }
  u64(what) { this.need(8, what); const v = this.buf.readBigUInt64LE(this.pos); this.pos += 8; return v; }
  bytes(n, what) { this.need(n, what); const b = this.buf.subarray(this.pos, this.pos + n); this.pos += n; return b; }
}

function readTag(r, v7, what) {
  const t = r.u8(what);
  if (t <= 9) return t;
  if ((t === T_F32 || t === T_F64) && v7) return t;
  refuse('UNSUPPORTED_TAG', what + ' tag ' + t);
}

// Register-like u16 field. 0x8000..0xFFFE never designates anything (spec 3.5
// RANGE; 10.1 INVALID). 0xFFFF is the "none" sentinel, checked per use.
function readReg(r, what) {
  const v = r.u16(what);
  if (v >= 0x8000 && v !== NONE) refuse('INVALID', what + ' operand 0x' + v.toString(16));
  return v;
}

function readFuncText(r, lenBytes, max, what) {
  const n = lenBytes === 1 ? r.u8(what + ' length') : r.u16(what + ' length');
  if (n > max) refuse('CAPACITY', what + ' length ' + n);
  const b = r.bytes(n, what);
  for (const c of b) if (c === 0) refuse('INVALID', what + ' contains NUL');
  return b;
}

function readRegion(r, i) {
  const what = 'region ' + i;
  const n = r.u8(what + ' name length');
  if (n === 0 || n > NAME_MAX) refuse('INVALID', what + ' name length ' + n);
  const nb = r.bytes(n, what + ' name');
  for (const c of nb) {
    if (c === 0 || c >= 0x80 || c === 0x2e) refuse('INVALID', what + ' name byte 0x' + c.toString(16));
  }
  const name = nb.toString('latin1');
  const tag = r.u8(what + ' element');
  if (!(isInt(tag) || isFloat(tag))) refuse('INVALID', what + ' element tag ' + tag);
  const cls = r.u8(what + ' class');
  if (cls === 3) refuse('UNSUPPORTED_OP', what + ' class 3 (workgroup)');
  if (cls > 3) refuse('INVALID', what + ' class ' + cls);
  const owner = r.u16(what + ' owner');
  const count = r.u32(what + ' count');
  if (count > MAX_COUNT) refuse('INVALID', what + ' count ' + count);
  if (cls !== 2 && count === 0) refuse('INVALID', what + ' mutable count 0');
  const initlen = r.u32(what + ' initial length');
  if (cls !== 2 && initlen !== 0) refuse('INVALID', what + ' initial data on class ' + cls);
  if (cls === 2 && tag !== 2) refuse('INVALID', what + ' text element tag ' + tag);
  if (cls === 2 && initlen !== count) refuse('INVALID', what + ' text length ' + initlen + ' != count ' + count);
  const init = Buffer.from(r.bytes(initlen, what + ' initial data'));
  return { name, tag, cls, owner, count, init };
}

function readInsn(r, v7, what) {
  const op = r.u8(what + ' opcode');
  const ins = { op, sub: 0, dst: NONE, a: NONE, b: NONE, c: NONE, imm: 0n, args: [] };
  const reg = (k) => readReg(r, what + ' ' + k);
  switch (op) {
    case OP.CONST: ins.dst = reg('dst'); ins.imm = r.u64(what + ' imm'); break;
    case OP.MOV: case OP.CAST: ins.dst = reg('dst'); ins.a = reg('a'); break;
    case OP.BIN: case OP.CMP:
      ins.sub = r.u8(what + ' sub'); ins.dst = reg('dst'); ins.a = reg('a'); ins.b = reg('b'); break;
    case OP.UN: ins.sub = r.u8(what + ' sub'); ins.dst = reg('dst'); ins.a = reg('a'); break;
    // LOAD/STORE (9/10) need a reference register, which this profile has not.
    case OP.CALL: {
      ins.dst = reg('dst'); ins.callee = reg('callee');
      const n = r.u8(what + ' nargs');
      if (n > MAX_PARAMS) refuse('CAPACITY', what + ' nargs ' + n);
      for (let k = 0; k < n; k++) ins.args.push(reg('arg' + k));
      break;
    }
    case OP.TRAP: ins.imm = BigInt(r.u8(what + ' trap code')); break;
    case OP.BR: ins.blkT = reg('target'); break;
    case OP.CBR: ins.a = reg('cond'); ins.blkT = reg('true target'); ins.blkF = reg('false target'); break;
    case OP.RET: ins.a = reg('a'); break;
    default:
      if (op >= 30 && op <= 33 && v7) {
        if (op === OP.RLOAD) { ins.dst = reg('dst'); ins.rg = reg('region'); ins.b = reg('index'); }
        else if (op === OP.RSTORE) { ins.rg = reg('region'); ins.b = reg('index'); ins.c = reg('src'); }
        else if (op === OP.BITCAST) { ins.dst = reg('dst'); ins.a = reg('src'); }
        else { ins.rg = reg('region'); ins.a = reg('init'); }
        break;
      }
      refuse('UNSUPPORTED_OP', what + ' opcode ' + op);
  }
  return ins;
}

function readFunc(r, v7, fi) {
  const what = 'function ' + fi;
  const nameBytes = readFuncText(r, 1, NAME_MAX, what + ' name');
  for (const c of nameBytes) if (c >= 0x80) refuse('INVALID', what + ' name byte 0x' + c.toString(16));
  const name = nameBytes.toString('latin1');
  const nparams = r.u8(what + ' nparams');
  const ret = readTag(r, v7, what + ' return');
  const nvregs = r.u16(what + ' nvregs');
  if (nvregs > MAX_VREGS) refuse('CAPACITY', what + ' nvregs ' + nvregs);
  const vtype = [];
  for (let k = 0; k < nvregs; k++) vtype.push(readTag(r, v7, what + ' register ' + k));
  const nblocks = r.u16(what + ' nblocks');
  if (nblocks > MAX_BLOCKS) refuse('CAPACITY', what + ' nblocks ' + nblocks);
  const blocks = [];
  for (let k = 0; k < nblocks; k++) blocks.push({ first: r.u32(what + ' block first'), count: r.u32(what + ' block count') });
  const ninsns = r.u32(what + ' ninsns');
  if (ninsns > MAX_INSNS) refuse('CAPACITY', what + ' ninsns ' + ninsns);
  const insns = [];
  for (let k = 0; k < ninsns; k++) insns.push(readInsn(r, v7, what + ' insn ' + k));
  readFuncText(r, 2, CLAUSE_MAX, what + ' requires');
  readFuncText(r, 2, CLAUSE_MAX, what + ' ensures');
  return { name, nameBytes, nparams, ret, nvregs, vtype, blocks, insns };
}

// ------------------------------------------------------------ validation
function canonicalConst(t, imm) {
  if (t === T_BOOL) return imm === 0n || imm === 1n;
  if (t === T_F64) return true;
  if (t === T_F32) return imm >> 32n === 0n; // M:f32upper
  const w = BigInt(intWidth(t));
  if (isSigned(t)) return BigInt.asUintN(64, BigInt.asIntN(Number(w), imm)) === imm;
  return imm >> w === 0n;
}

function validateFunc(u, fi) {
  const f = u.funcs[fi];
  const bad = (m) => refuse('INVALID', 'function ' + fi + ' (' + f.name + '): ' + m);
  if (f.nparams > MAX_PARAMS) bad('nparams ' + f.nparams);
  if (f.nparams > f.nvregs) bad('nparams exceeds nvregs');
  if (!(f.ret === T_VOID || isValue(f.ret))) bad('return tag ' + f.ret);
  f.vtype.forEach((t, k) => { if (!isValue(t)) bad('register ' + k + ' tag ' + t); });
  if (f.blocks.length === 0 || f.insns.length === 0) bad('empty body');
  let next = 0;
  f.blocks.forEach((b, k) => {
    if (b.first !== next || b.count === 0) bad('block ' + k + ' does not partition');
    next = b.first + b.count;
    if (next > f.insns.length) bad('block ' + k + ' past end');
  });
  if (next !== f.insns.length) bad('blocks do not cover instructions');
  f.blocks.forEach((b, k) => {
    for (let j = b.first; j < b.first + b.count; j++) {
      const last = j === b.first + b.count - 1;
      if (isTerm(f.insns[j].op) !== last) bad('block ' + k + ' terminator placement');
    }
  });

  const ty = (r, k) => {
    if (r === NONE || r >= f.nvregs) bad(k + ' register ' + r + ' outside table');
    return f.vtype[r];
  };
  const region = (g, k) => {
    if (g === NONE || g >= u.regions.length) bad(k + 'region ' + g + ' outside table');
    const rg = u.regions[g];
    if (rg.cls === 1 && rg.owner !== fi) bad(k + 'region ' + rg.name + ' owned by function ' + rg.owner); // M:owner
    return rg;
  };
  const blk = (b) => { if (b === NONE || b >= f.blocks.length) bad('branch target ' + b); };

  f.insns.forEach((ins, j) => {
    const at = 'insn ' + j + ' ';
    switch (ins.op) {
      case OP.CONST: if (!canonicalConst(ty(ins.dst, at + 'dst'), ins.imm)) bad(at + 'non-canonical constant'); break;
      case OP.MOV: if (ty(ins.dst, at + 'dst') !== ty(ins.a, at + 'a')) bad(at + 'MOV type'); break;
      case OP.BIN: {
        const t = ty(ins.dst, at + 'dst');
        if (ty(ins.a, at + 'a') !== t) bad(at + 'BIN a type');
        const tb = ty(ins.b, at + 'b');
        const s = ins.sub;
        if (s < 1 || s > 10) bad(at + 'BIN sub ' + s);
        if (isFloat(t)) { if (s > 4 || tb !== t) bad(at + 'float BIN sub ' + s); }
        else if (t === T_BOOL) { if (s < 6 || s > 8 || tb !== t) bad(at + 'bool BIN sub ' + s); }
        else if (s >= 9) { if (!isInt(tb)) bad(at + 'shift amount type'); }
        else if (tb !== t) bad(at + 'BIN b type');
        break;
      }
      case OP.UN: {
        const t = ty(ins.dst, at + 'dst');
        if (ty(ins.a, at + 'a') !== t) bad(at + 'UN type');
        const ok = ins.sub === 1 ? isInt(t) || isFloat(t) : ins.sub === 2 ? isInt(t) : ins.sub === 3 ? t === T_BOOL : false;
        if (!ok) bad(at + 'UN sub ' + ins.sub + ' on tag ' + t);
        break;
      }
      case OP.CMP:
        if (ins.sub < 1 || ins.sub > 6) bad(at + 'CMP sub ' + ins.sub);
        if (ty(ins.dst, at + 'dst') !== T_BOOL) bad(at + 'CMP dst not bool');
        if (ty(ins.a, at + 'a') !== ty(ins.b, at + 'b')) bad(at + 'CMP operand types');
        break;
      case OP.CAST: {
        const d = ty(ins.dst, at + 'dst'), s = ty(ins.a, at + 'a');
        if (!(isInt(d) || isFloat(d)) || !(isInt(s) || isFloat(s))) bad(at + 'CAST types');
        break;
      }
      case OP.CALL: {
        if (ins.callee === NONE || ins.callee >= fi) bad(at + 'callee ' + ins.callee + ' is not an earlier function');
        const g = u.funcs[ins.callee];
        if (ins.args.length !== g.nparams) bad(at + 'CALL arity');
        ins.args.forEach((a, k) => { if (ty(a, at + 'arg') !== g.vtype[k]) bad(at + 'CALL arg ' + k + ' type'); });
        if (g.ret === T_VOID) { if (ins.dst !== NONE) bad(at + 'void call with destination'); }
        else if (ty(ins.dst, at + 'dst') !== g.ret) bad(at + 'CALL result type');
        break;
      }
      case OP.TRAP: if (ins.imm < 1n || ins.imm > 14n) bad(at + 'trap code ' + ins.imm); break;
      case OP.BR: blk(ins.blkT); break;
      case OP.CBR:
        if (ty(ins.a, at + 'cond') !== T_BOOL) bad(at + 'CBR condition not bool');
        blk(ins.blkT); blk(ins.blkF);
        break;
      case OP.RET:
        if (f.ret === T_VOID) { if (ins.a !== NONE) bad(at + 'value returned from void'); }
        else if (ty(ins.a, at + 'a') !== f.ret) bad(at + 'RET type');
        break;
      case OP.RLOAD: {
        const t = ty(ins.dst, at + 'dst');
        const rg = region(ins.rg, at);
        if (!isInt(ty(ins.b, at + 'index'))) bad(at + 'index type');
        if (t !== rg.tag) bad(at + 'RLOAD element type');
        break;
      }
      case OP.RSTORE: {
        const rg = region(ins.rg, at);
        if (rg.cls === 2) bad(at + 'RSTORE into read-only text ' + rg.name); // M:textro
        if (!isInt(ty(ins.b, at + 'index'))) bad(at + 'index type');
        if (ty(ins.c, at + 'src') !== rg.tag) bad(at + 'RSTORE element type');
        break;
      }
      case OP.BITCAST: {
        const d = ty(ins.dst, at + 'dst'), s = ty(ins.a, at + 'src');
        if (BITCAST_SET[d] === undefined || BITCAST_SET[d] !== BITCAST_SET[s]) bad(at + 'BITCAST ' + s + '->' + d);
        break;
      }
      case OP.RINIT: {
        const rg = region(ins.rg, at);
        if (rg.cls !== 1) bad(at + 'RINIT of non-local region ' + rg.name);
        if (ty(ins.a, at + 'init') !== rg.tag) bad(at + 'RINIT type');
        break;
      }
      default: bad(at + 'opcode ' + ins.op);
    }
  });
  definiteAssignment(u, f, fi, bad);
}

// Uses and definitions of one instruction (registers only).
function usesOf(ins) {
  switch (ins.op) {
    case OP.MOV: case OP.UN: case OP.CAST: case OP.BITCAST: case OP.RET: case OP.CBR: case OP.RINIT:
      return ins.a === NONE ? [] : [ins.a];
    case OP.BIN: case OP.CMP: return [ins.a, ins.b];
    case OP.CALL: return ins.args;
    case OP.RLOAD: return [ins.b];
    case OP.RSTORE: return [ins.b, ins.c];
    default: return [];
  }
}
function defOf(ins) {
  switch (ins.op) {
    case OP.CONST: case OP.MOV: case OP.BIN: case OP.UN: case OP.CMP: case OP.CAST:
    case OP.RLOAD: case OP.BITCAST: case OP.CALL:
      return ins.dst === NONE ? -1 : ins.dst;
    default: return -1;
  }
}

// Greatest-fixed-point must-analysis over registers and local-region
// initialization bits (spec 10.2). State vector: nvregs register bits, then
// one bit per region (only class-1 regions owned by this function matter).
function definiteAssignment(u, f, fi, bad) {
  const nb = f.blocks.length, nr = f.nvregs, ng = u.regions.length, n = nr + ng;
  const entry = new Uint8Array(n);
  for (let k = 0; k < f.nparams; k++) entry[k] = 1;
  const succ = f.blocks.map((b) => {
    const t = f.insns[b.first + b.count - 1];
    return t.op === OP.BR ? [t.blkT] : t.op === OP.CBR ? [t.blkT, t.blkF] : [];
  });
  const reach = new Uint8Array(nb);
  const stack = [0];
  reach[0] = 1;
  while (stack.length) {
    for (const s of succ[stack.pop()]) if (!reach[s]) { reach[s] = 1; stack.push(s); }
  }
  const preds = Array.from({ length: nb }, () => []);
  succ.forEach((ss, b) => { if (reach[b]) for (const s of ss) preds[s].push(b); });

  const transfer = (b, inState, check) => {
    const st = Uint8Array.from(inState);
    const blk = f.blocks[b];
    for (let j = blk.first; j < blk.first + blk.count; j++) {
      const ins = f.insns[j];
      if (check) {
        for (const r of usesOf(ins)) if (!st[r]) bad('insn ' + j + ' uses register ' + r + ' before definite assignment');
        if ((ins.op === OP.RLOAD || ins.op === OP.RSTORE) && u.regions[ins.rg].cls === 1 && !st[nr + ins.rg]) { // M:initcheck
          bad('insn ' + j + ' accesses local region ' + u.regions[ins.rg].name + ' before RINIT on every path');
        }
      }
      const d = defOf(ins);
      if (d >= 0) st[d] = 1;
      if (ins.op === OP.RINIT) st[nr + ins.rg] = 1;
    }
    return st;
  };

  // Reachable blocks start at TOP (everything assigned) and only shrink.
  const out = f.blocks.map((_, b) => (reach[b] ? new Uint8Array(n).fill(1) : null));
  const inOf = (b) => {
    if (!reach[b]) return entry; // unreachable: validated from the entry state
    const st = b === 0 ? Uint8Array.from(entry) : new Uint8Array(n).fill(1);
    for (const p of preds[b]) {
      const o = out[p];
      for (let k = 0; k < n; k++) st[k] &= o[k]; // M:meet
    }
    return st;
  };
  let changed = true;
  while (changed) {
    changed = false;
    for (let b = 0; b < nb; b++) {
      if (!reach[b]) continue;
      const o = transfer(b, inOf(b), false);
      for (let k = 0; k < n; k++) if (o[k] !== out[b][k]) { out[b] = o; changed = true; break; }
    }
  }
  for (let b = 0; b < nb; b++) transfer(b, inOf(b), true);
}

function validateUnit(u) {
  const bad = (m) => refuse('INVALID', m);
  const module = new Set();
  u.regions.forEach((rg, i) => {
    if (rg.cls === 1) {
      if (rg.owner === NONE || rg.owner >= u.funcs.length) bad('region ' + i + ' local owner ' + rg.owner);
    } else {
      if (rg.owner !== NONE) bad('region ' + i + ' module owner ' + rg.owner);
      if (module.has(rg.name)) bad('duplicate module region ' + rg.name);
      module.add(rg.name);
    }
  });
  const locals = new Set();
  u.regions.forEach((rg) => {
    if (rg.cls !== 1) return;
    if (module.has(rg.name)) bad('local region ' + rg.name + ' shadows a module region');
    const key = rg.owner + '/' + rg.name;
    if (locals.has(key)) bad('duplicate local region ' + rg.name + ' in function ' + rg.owner);
    locals.add(key);
  });
  const exportsSeen = new Set();
  const exp = (name) => { if (exportsSeen.has(name)) bad('export name collision ' + JSON.stringify(name)); exportsSeen.add(name); }; // M:export
  if (u.version === 7) {
    exp('memory');
    u.regions.forEach((rg) => { if (rg.cls !== 1) exp('region.' + rg.name); });
  }
  u.funcs.forEach((f) => exp(f.name));
}

function usesV7(u) {
  if (u.regions.length > 0) return true;
  for (const f of u.funcs) {
    if (isFloat(f.ret) || f.vtype.some(isFloat)) return true;
    if (f.insns.some((i) => i.op >= 30 && i.op <= 33)) return true;
  }
  return false;
}

function decode(buf) {
  if (buf.length > V7_INPUT_MAX) refuse('CAPACITY', 'input ' + buf.length + ' bytes');
  const headIsV7 = buf.length >= 8 && MAGIC.every((m, i) => buf[i] === m) && buf[7] === 7;
  if (buf.length > V1_INPUT_MAX && !headIsV7) refuse('CAPACITY', 'input ' + buf.length + ' bytes');
  const r = new Reader(buf);
  for (let i = 0; i < 7; i++) if (r.u8('magic') !== MAGIC[i]) refuse('MAGIC', 'byte ' + i);
  const version = r.u8('version');
  if (version !== 1 && version !== 7) refuse('VERSION', 'version ' + version);
  const v7 = version === 7;
  const u = { version, regions: [], funcs: [] };
  if (v7) {
    const ns = r.u8('nstructs');
    if (ns > MAX_STRUCTS) refuse('CAPACITY', 'nstructs ' + ns);
    if (ns > 0) refuse('UNSUPPORTED_TAG', 'struct table (' + ns + ' structs)');
    const ng = r.u16('nregions');
    if (ng > MAX_REGIONS) refuse('INVALID', 'nregions ' + ng);
    for (let i = 0; i < ng; i++) u.regions.push(readRegion(r, i));
  }
  const nf = r.u16('nfuncs');
  if (nf > MAX_FUNCS) refuse('CAPACITY', 'nfuncs ' + nf);
  for (let i = 0; i < nf; i++) {
    u.funcs.push(readFunc(r, v7, i));
    validateFunc(u, i);
  }
  validateUnit(u);
  if (r.pos !== buf.length) refuse('TRAILING', (buf.length - r.pos) + ' bytes after last function'); // M:trailing
  if (v7 && !usesV7(u)) refuse('VERSION', 'gratuitous v7: no region, float or memory opcode'); // M:gratuitous
  return u;
}

// ----------------------------------------------------------- interpreter
// Integer and bool registers hold their mathematical value as a BigInt
// (unsigned 0..2^w-1, signed sign-extended, bool 0/1). Float registers hold
// their raw IEEE bits as a BigInt so CONST/MOV/RLOAD/RSTORE/RINIT/BITCAST
// transport NaN payloads exactly; arithmetic converts through a DataView.
class Trap { constructor(code) { this.code = code; } }
const trap = (code) => { throw new Trap(code); };

const scratch = new DataView(new ArrayBuffer(8));
function f32val(bits) { scratch.setUint32(0, Number(bits), true); return scratch.getFloat32(0, true); }
function f32bits(x) { scratch.setFloat32(0, x, true); return BigInt(scratch.getUint32(0, true)); }
function f64val(bits) { scratch.setBigUint64(0, bits, true); return scratch.getFloat64(0, true); }
function f64bits(x) { scratch.setFloat64(0, x, true); return scratch.getBigUint64(0, true); }
const fval = (t, bits) => (t === T_F32 ? f32val(bits) : f64val(bits));
const fbits = (t, x) => (t === T_F32 ? f32bits(Math.fround(x)) : f64bits(x));

function intMin(t) { return isSigned(t) ? -(1n << BigInt(intWidth(t) - 1)) : 0n; }
function intMax(t) { return isSigned(t) ? (1n << BigInt(intWidth(t) - 1)) - 1n : (1n << BigInt(intWidth(t))) - 1n; }
function wrapInt(t, v) { const w = intWidth(t); return isSigned(t) ? BigInt.asIntN(w, v) : BigInt.asUintN(w, v); }
function checked(t, v) {
  if (!isSigned(t)) return BigInt.asUintN(intWidth(t), v);
  if (v < intMin(t) || v > intMax(t)) trap(1);
  return v;
}

// Integer to f32 with one rounding (nearest, ties to even), from the exact
// BigInt. Converting through a double first could round twice.
function bigToF32Bits(v) {
  const neg = v < 0n;
  let m = neg ? -v : v;
  let x;
  if (m < (1n << 24n)) x = Number(m);
  else {
    const shift = m.toString(2).length - 24;
    let top = m >> BigInt(shift);
    const rest = m - (top << BigInt(shift));
    const half = 1n << BigInt(shift - 1);
    if (rest > half || (rest === half && (top & 1n) === 1n)) top += 1n;
    x = Number(top) * 2 ** shift; // M:i2f32
  }
  return f32bits(neg ? -x : x);
}

function castValue(dt, st, v) {
  if (isInt(st) && isInt(dt)) {
    if (v < intMin(dt) || v > intMax(dt)) trap(5);
    return v;
  }
  if (isFloat(st) && isInt(dt)) {
    const x = fval(st, v);
    if (Number.isNaN(x)) return 0n; // M:satnan
    if (x === Infinity) return intMax(dt);
    if (x === -Infinity) return intMin(dt);
    const t = BigInt(Math.trunc(x));
    if (t < intMin(dt)) return intMin(dt); // M:satlow
    if (t > intMax(dt)) return intMax(dt); // M:sathigh
    return t;
  }
  if (isInt(st) && isFloat(dt)) return dt === T_F32 ? bigToF32Bits(v) : f64bits(Number(v));
  if (st === dt) return v;
  return fbits(dt, fval(st, v));
}

function bitcastValue(dt, st, v) {
  let bits;
  if (isFloat(st)) bits = v;
  else bits = BigInt.asUintN(intWidth(st), v);
  if (isFloat(dt)) return bits;
  return isSigned(dt) ? BigInt.asIntN(intWidth(dt), bits) : bits; // M:bcsign
}

function binInt(t, sub, a, b, tb) {
  const w = intWidth(t);
  switch (sub) {
    case 1: return checked(t, a + b);
    case 2: return checked(t, a - b);
    case 3: return checked(t, a * b);
    case 4:
      if (b === 0n) trap(2);
      return checked(t, a / b);
    case 5:
      if (b === 0n) trap(2);
      return a % b;
    case 6: return a & b;
    case 7: return a | b;
    case 8: return a ^ b;
    case 9:
      if (b < 0n || b >= BigInt(w)) trap(7);
      return isSigned(t) ? checked(t, a << b) : BigInt.asUintN(w, a << b);
    case 10:
      if (b < 0n || b >= BigInt(w)) trap(7);
      return a >> b;
  }
  throw new Error('unreachable BIN ' + sub + ' ' + tb);
}

function binFloat(t, sub, a, b) {
  const x = fval(t, a), y = fval(t, b);
  const r = sub === 1 ? x + y : sub === 2 ? x - y : sub === 3 ? x * y : x / y;
  return fbits(t, r);
}

function cmpValues(sub, x, y) {
  switch (sub) {
    case 1: return x === y;
    case 2: return !(x === y);
    case 3: return x < y;
    case 4: return x <= y;
    case 5: return x > y;
    case 6: return x >= y;
  }
  return false;
}

class Region {
  constructor(rg) {
    this.rg = rg;
    this.w = elemBytes(rg.tag);
    this.mem = new Uint8Array(rg.count * this.w);
    if (rg.cls === 2) this.mem.set(rg.init);
  }
  index(i) {
    if (i < 0n || i >= BigInt(this.rg.count)) trap(3); // M:bounds
    return Number(i) * this.w;
  }
  elemBits(v) { return isFloat(this.rg.tag) ? v : BigInt.asUintN(8 * this.w, v); }
  load(i) {
    const off = this.index(i);
    let bits = 0n;
    for (let k = this.w - 1; k >= 0; k--) bits = (bits << 8n) | BigInt(this.mem[off + k]);
    const t = this.rg.tag;
    if (isFloat(t)) return bits;
    return isSigned(t) ? BigInt.asIntN(8 * this.w, bits) : bits;
  }
  writeAt(off, bits) {
    for (let k = 0; k < this.w; k++) { this.mem[off + k] = Number(bits & 0xffn); bits >>= 8n; }
  }
  store(i, v) { this.writeAt(this.index(i), this.elemBits(v)); }
  fill(v) {
    this.writeAt(0, this.elemBits(v));
    for (let have = this.w; have < this.mem.length; have *= 2) {
      this.mem.copyWithin(have, 0, Math.min(have, this.mem.length - have));
    }
  }
  hex() { return Buffer.from(this.mem).toString('hex'); }
}

class Instance {
  constructor(u) {
    this.u = u;
    this.steps = 0;
    this.module = u.regions.map((rg) => (rg.cls === 1 ? null : new Region(rg)));
  }
  call(fi, args) {
    const u = this.u, f = u.funcs[fi];
    const regs = new Array(f.nvregs).fill(0n);
    for (let k = 0; k < args.length; k++) regs[k] = args[k];
    // Fresh local storage on every invocation (spec 10.1 class 1).
    const local = u.regions.map((rg) => (rg.cls === 1 && rg.owner === fi ? new Region(rg) : null));
    const reg = (g) => this.module[g] || local[g];
    let b = 0;
    for (;;) {
      const blk = f.blocks[b];
      let nextBlock = -1;
      for (let j = blk.first; j < blk.first + blk.count; j++) {
        if (++this.steps > STEP_LIMIT) throw new Error('step limit ' + STEP_LIMIT + ' exceeded');
        const ins = f.insns[j];
        const T = (r) => f.vtype[r];
        switch (ins.op) {
          case OP.CONST: regs[ins.dst] = isSigned(T(ins.dst)) ? BigInt.asIntN(intWidth(T(ins.dst)), ins.imm) : ins.imm; break;
          case OP.MOV: regs[ins.dst] = regs[ins.a]; break;
          case OP.BIN: {
            const t = T(ins.dst);
            if (isFloat(t)) regs[ins.dst] = binFloat(t, ins.sub, regs[ins.a], regs[ins.b]);
            else regs[ins.dst] = binInt(t, ins.sub, regs[ins.a], regs[ins.b], T(ins.b));
            break;
          }
          case OP.UN: {
            const t = T(ins.dst), a = regs[ins.a];
            if (ins.sub === 1) {
              if (t === T_F32) regs[ins.dst] = a ^ 0x80000000n; // M:fneg
              else if (t === T_F64) regs[ins.dst] = a ^ 0x8000000000000000n;
              else regs[ins.dst] = isSigned(t) ? checked(t, -a) : wrapInt(t, -a);
            } else if (ins.sub === 2) regs[ins.dst] = wrapInt(t, ~a);
            else regs[ins.dst] = 1n - a;
            break;
          }
          case OP.CMP: {
            const t = T(ins.a);
            const x = isFloat(t) ? fval(t, regs[ins.a]) : regs[ins.a];
            const y = isFloat(t) ? fval(t, regs[ins.b]) : regs[ins.b];
            regs[ins.dst] = cmpValues(ins.sub, x, y) ? 1n : 0n;
            break;
          }
          case OP.CAST: regs[ins.dst] = castValue(T(ins.dst), T(ins.a), regs[ins.a]); break;
          case OP.CALL: {
            const r = this.call(ins.callee, ins.args.map((a) => regs[a]));
            if (ins.dst !== NONE) regs[ins.dst] = r;
            break;
          }
          case OP.TRAP: trap(Number(ins.imm)); break;
          case OP.BR: nextBlock = ins.blkT; break;
          case OP.CBR: nextBlock = regs[ins.a] === 1n ? ins.blkT : ins.blkF; break;
          case OP.RET: return ins.a === NONE ? null : regs[ins.a];
          case OP.RLOAD: regs[ins.dst] = reg(ins.rg).load(regs[ins.b]); break;
          case OP.RSTORE: reg(ins.rg).store(regs[ins.b], regs[ins.c]); break;
          case OP.BITCAST: regs[ins.dst] = bitcastValue(T(ins.dst), T(ins.a), regs[ins.a]); break;
          case OP.RINIT: reg(ins.rg).fill(regs[ins.a]); break;
          default: throw new Error('unreachable opcode ' + ins.op);
        }
      }
      b = nextBlock;
    }
  }
}

// ------------------------------------------------------------------- CLI
function usage(msg) {
  process.stderr.write('oracle: ' + msg + '\n' +
    'usage: oracle.js check UNIT\n       oracle.js run [--codes] UNIT SCRIPT\n');
  process.exit(2);
}

function readFile(p) {
  try { return fs.readFileSync(p); } catch (e) { usage('cannot read ' + p + ': ' + e.code); }
}

function decodeOrRefusal(buf) {
  try { return { unit: decode(buf) }; } catch (e) {
    if (e instanceof Refusal) return { refusal: e };
    throw e;
  }
}

function parseArg(t, s) {
  if (isFloat(t)) {
    const digits = t === T_F32 ? 8 : 16;
    if (!new RegExp('^0x[0-9a-fA-F]{' + digits + '}$').test(s)) return null;
    return BigInt(s);
  }
  if (!/^-?[0-9]+$/.test(s)) return null;
  const v = BigInt(s);
  if (t === T_BOOL) return v === 0n || v === 1n ? v : null;
  return v >= intMin(t) && v <= intMax(t) ? v : null;
}

function formatValue(t, v) {
  if (t === T_VOID) return 'void';
  if (t === T_F32) return '0x' + v.toString(16).padStart(8, '0');
  if (t === T_F64) return '0x' + v.toString(16).padStart(16, '0');
  return v.toString(10);
}

function main(argv) {
  const codes = argv.includes('--codes');
  const args = argv.filter((a) => a !== '--codes');
  const cmd = args[0];
  if (cmd === 'check' && args.length === 2 && !codes) {
    const res = decodeOrRefusal(readFile(args[1]));
    if (res.refusal) {
      process.stdout.write('REFUSE ' + res.refusal.cls + ' ' + res.refusal.detail + '\n');
      return 1;
    }
    process.stdout.write('ACCEPT v' + res.unit.version + '\n');
    return 0;
  }
  if (cmd === 'run' && args.length === 3) {
    const res = decodeOrRefusal(readFile(args[1]));
    if (res.refusal) {
      process.stderr.write('REFUSE ' + res.refusal.cls + ' ' + res.refusal.detail + '\n');
      return 1;
    }
    const u = res.unit;
    const script = readFile(args[2]).toString('utf8').split('\n');
    const inst = new Instance(u);
    const out = [];
    const flush = () => { if (out.length) process.stdout.write(out.join('')); out.length = 0; };
    for (let ln = 0; ln < script.length; ln++) {
      const line = script[ln].trim();
      if (line === '' || line.startsWith('#')) continue;
      const w = line.split(/\s+/);
      const bad = (m) => { flush(); usage('script line ' + (ln + 1) + ': ' + m); };
      if (w[0] === 'call') {
        const fi = u.funcs.findIndex((f) => f.name === w[1]);
        if (fi < 0) bad('no function ' + w[1]);
        const f = u.funcs[fi];
        if (w.length - 2 !== f.nparams) bad('expected ' + f.nparams + ' arguments');
        const vals = w.slice(2).map((s, k) => {
          const v = parseArg(f.vtype[k], s);
          if (v === null) bad('argument ' + s + ' does not fit tag ' + f.vtype[k]);
          return v;
        });
        try {
          const r = inst.call(fi, vals);
          out.push(f.name + ' = ' + formatValue(f.ret, r) + '\n');
        } catch (e) {
          if (!(e instanceof Trap)) { flush(); process.stderr.write('oracle: ' + e.message + '\n'); return 3; }
          out.push(f.name + ' TRAP' + (codes ? ' code=' + e.code : '') + '\n');
        }
      } else if (w[0] === 'region' && w.length === 2) {
        const g = u.regions.findIndex((rg) => rg.cls !== 1 && rg.name === w[1]);
        if (g < 0) bad('no module region ' + w[1]);
        out.push('region ' + w[1] + ' ' + inst.module[g].hex() + '\n');
      } else bad('unknown command ' + JSON.stringify(line));
    }
    flush();
    return 0;
  }
  usage('bad arguments');
}

process.exitCode = main(process.argv.slice(2));
