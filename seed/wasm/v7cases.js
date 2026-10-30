#!/usr/bin/env node
// SPDX-License-Identifier: Apache-2.0
// irwasm v7 run cases: unit descriptions and scripts for gate-irwasm.sh.
//
//   node seed/wasm/v7cases.js OUTDIR
// writes OUTDIR/<case>.unit and OUTDIR/<case>.script. The bytes come from the
// independent hand writer seed/wasm/v7/writer.js; neither file shares code
// with irwasm.om. Scripts use the oracle `run` format (seed/wasm/v7/oracle.js).
// Float arguments are raw IEEE bit patterns; every pattern below was
// computed with Wolfram (query quoted in the PR that added this file).
// NaN payloads never cross the JS Number boundary here: they are checked
// through region bytes and BITCAST to integers (case `nanpay`).
'use strict';

const fs = require('fs');
const path = require('path');
const { encode, fixtures } = require('./v7/writer.js');

const mod = (name, tag, count) => ({ name, tag, cls: 0, owner: 0xffff, count });
const text = (name, s) => ({ name, tag: 'u8', cls: 2, owner: 0xffff, count: s.length, init: s });
const loc = (name, tag, count, owner) => ({ name, tag, cls: 1, owner, count });
const cases = {};
const W = 16, H = 8;

// RGBX gradient: fill() writes W*H packed u32 pixels into the global
// framebuffer `fb`; the byte lane of each channel is its position in the text
// region `order`, found by shift(c). R = 16x, G = 32y, B = 8(x+y), X = 255.
function rgbx(order, bmul) {
  return {
    version: 7,
    regions: [mod('fb', 'u32', W * H), text('order', order)],
    funcs: [
      // shift(c u8) -> u32: 8 * (index of c in order)
      { name: 'shift', params: ['u8'], ret: 'u32', regs: ['u32', 'u8', 'bool', 'u32', 'u32', 'u32'],
        blocks: [
          [['const', 1, 0], ['br', 1]],
          [['rload', 2, 1, 1], ['cmp', 'eq', 3, 2, 0], ['cbr', 3, 2, 3]],
          [['const', 4, 8], ['bin', 'mul', 5, 1, 4], ['ret', 5]],
          [['const', 6, 1], ['bin', 'add', 1, 1, 6], ['br', 1]],
        ] },
      // fill() -> u32: pixel count written
      { name: 'fill', params: [], ret: 'u32',
        // 0 i, 1 n, 2 w, 3 cond, 4 x, 5 y, 6 k, 7 R, 8 G, 9 B, 10 X,
        // 11 c, 12 s, 13 p, 14 q, 15 one
        regs: ['u32', 'u32', 'u32', 'bool', 'u32', 'u32', 'u32', 'u32', 'u32', 'u32', 'u32',
          'u8', 'u32', 'u32', 'u32', 'u32'],
        blocks: [
          [['const', 0, 0], ['const', 1, W * H], ['const', 2, W], ['br', 1]],
          [['cmp', 'lt', 3, 0, 1], ['cbr', 3, 2, 3]],
          [['bin', 'rem', 4, 0, 2], ['bin', 'div', 5, 0, 2],
            ['const', 6, 16], ['bin', 'mul', 7, 4, 6],
            ['const', 6, 32], ['bin', 'mul', 8, 5, 6],
            ['bin', 'add', 9, 4, 5], ['const', 6, bmul], ['bin', 'mul', 9, 9, 6],
            ['const', 10, 255],
            ['const', 11, 82], ['call', 12, 0, 11], ['bin', 'shl', 13, 7, 12],
            ['const', 11, 71], ['call', 12, 0, 11], ['bin', 'shl', 14, 8, 12], ['bin', 'or', 13, 13, 14],
            ['const', 11, 66], ['call', 12, 0, 11], ['bin', 'shl', 14, 9, 12], ['bin', 'or', 13, 13, 14],
            ['const', 11, 88], ['call', 12, 0, 11], ['bin', 'shl', 14, 10, 12], ['bin', 'or', 13, 13, 14],
            ['rstore', 0, 0, 13], ['const', 15, 1], ['bin', 'add', 0, 0, 15], ['br', 1]],
          [['ret', 1]],
        ] },
    ],
  };
}
const rgbxScript = 'region fb\ncall fill\nregion fb\nregion order\ncall shift 88\n';
cases.rgbx = [rgbx('RGBX', 8), rgbxScript];
// Mutants for the checksum negative controls: B channel 9(x+y), and order BGRX.
cases.rgbx_mut_b = [rgbx('RGBX', 9), rgbxScript];
cases.rgbx_mut_order = [rgbx('BGRX', 8), rgbxScript];

// Out-of-bounds: module region g (u32 x 5) and a local region l (u8 x 3),
// indexed through every integer width. Index == count, count + 1, negative,
// and wide values whose low 32 bits are in range must all trap.
const ld = (name, idx) => ({ name, params: [idx], ret: 'u32', regs: ['u32'], blocks: [[['rload', 1, 0, 0], ['ret', 1]]] });
cases.oob = [{
  version: 7,
  regions: [mod('g', 'u32', 5), loc('l', 'u8', 3, 6)],
  funcs: [
    ld('gu32', 'u32'), ld('gi32', 'i32'), ld('gi64', 'i64'), ld('gu64', 'u64'), ld('gi8', 'i8'),
    { name: 'si64', params: ['i64', 'u32'], ret: 'void', blocks: [[['rstore', 0, 0, 1], ['ret', 'none']]] },
    { name: 'lu64', params: ['u64'], ret: 'u8', regs: ['u8', 'u8'],
      blocks: [[['const', 1, 9], ['rinit', 1, 1], ['rload', 2, 1, 0], ['ret', 2]]] },
  ],
}, [
  'call si64 0 11', 'call si64 4 44', 'call gu32 0', 'call gu32 4',
  'call gu32 5', 'call gu32 6', 'call gu32 4294967295',
  'call gi32 -1', 'call gi32 -2147483648', 'call gi32 2147483647', 'call gi32 4',
  'call gi64 -1', 'call gi64 -9223372036854775808', 'call gi64 4294967296', 'call gi64 4294967300', 'call gi64 3',
  'call gu64 18446744073709551615', 'call gu64 4294967296', 'call gu64 4294967300', 'call gu64 5', 'call gu64 2',
  'call gi8 -1', 'call gi8 -128', 'call gi8 5', 'call gi8 1',
  'call si64 5 99', 'call si64 -1 99', 'call si64 4294967296 99', 'region g',
  'call lu64 2', 'call lu64 3', 'call lu64 4294967298', 'call lu64 18446744073709551615',
].join('\n') + '\n'];

// NaN payloads through CONST -> RSTORE -> region bytes, and RLOAD -> BITCAST -> integer.
cases.nanpay = [{
  version: 7,
  regions: [mod('nf', 'f32', 2), mod('nd', 'f64', 2)],
  funcs: [
    { name: 'put', ret: 'void', regs: ['u64', 'f32', 'f64', 'u64', 'f32', 'f64'],
      blocks: [[['const', 0, 0], ['const', 1, 0x7fa00001], ['rstore', 0, 0, 1],
        ['const', 2, 0xfff0000000000001n], ['rstore', 1, 0, 2],
        ['const', 3, 1], ['const', 4, 0xffc00123], ['rstore', 0, 3, 4],
        ['const', 5, 0x7ff8000000000abcn], ['rstore', 1, 3, 5], ['ret', 'none']]] },
    { name: 'fbits', params: ['u32'], ret: 'u32', regs: ['f32', 'u32'],
      blocks: [[['rload', 1, 0, 0], ['bitcast', 2, 1], ['ret', 2]]] },
    { name: 'dbits', params: ['u32'], ret: 'u64', regs: ['f64', 'u64'],
      blocks: [[['rload', 1, 1, 0], ['bitcast', 2, 1], ['ret', 2]]] },
    // u32 -> f32 bits -> negate -> back: sign flip keeps the payload
    { name: 'negbits', params: ['u32'], ret: 'u32', regs: ['f32', 'f32', 'u32'],
      blocks: [[['bitcast', 1, 0], ['un', 'neg', 2, 1], ['bitcast', 3, 2], ['ret', 3]]] },
  ],
}, 'region nf\nregion nd\ncall put\nregion nf\nregion nd\ncall fbits 0\ncall fbits 1\n' +
  'call dbits 0\ncall dbits 1\ncall negbits 2141192193\ncall negbits 1065353216\ncall fbits 2\n'];

// Scripts for the accepting fixtures of the independent writer.
const F64 = {
  nan: '0x7ff8000000000000', inf: '0x7ff0000000000000', ninf: '0xfff0000000000000',
  nzero: '0x8000000000000000', zero: '0x0000000000000000',
  p39: '0x400f333333333333', n39: '0xc00f333333333333', i32hi: '0x41dfffffffe00000',
  t31: '0x41e0000000000000', i32lo: '0xc1e0000000200000', big: '0x7e37e43c8800759c',
  nbig: '0xfe37e43c8800759c', u8hi: '0x406ffccccccccccd', t8: '0x4070000000000000',
  m1: '0xbff0000000000000', mhalf: '0xbfe0000000000000', t63: '0x43e0000000000000',
  nt63: '0xc3e0000000000000', t64: '0x43f0000000000000', u64hi: '0x43efffffffffffff',
  i64hi: '0x43dfffffffffffff', i64lo: '0xc3e0000000000001', tenth: '0x3fb999999999999a',
  p15: '0x3ff8000000000000', p25: '0x4004000000000000', k1: '0x408f400000000000',
};
const F32 = {
  nan: '0x7fc00000', inf: '0x7f800000', ninf: '0xff800000', nzero: '0x80000000',
  p39: '0x4079999a', n39: '0xc079999a', t7: '0x43000000', n129: '0xc3010000', p127: '0x42ff0000',
  t32: '0x4f800000', u32hi: '0x4f7fffff', m1: '0xbf800000', i16hi: '0x46ffff00', i16lo: '0xc7000100',
  big: '0x7149f2ca', tenth: '0x3dcccccd', p15: '0x3fc00000', t31: '0x4f000000', nt31: '0xcf000000',
  k1: '0x447a0000',
};
const calls = (name, args) => args.map((a) => 'call ' + name + ' ' + a).join('\n') + '\n';
const d = (...k) => k.map((x) => F64[x]);
const f = (...k) => k.map((x) => F32[x]);
// Signed narrow elements: sign extension on RLOAD, truncation on RSTORE.
const sg = (tag) => [
  { name: 'set' + tag, params: ['u32', tag], ret: 'void', blocks: [[['rstore', tag === 'i8' ? 0 : 1, 0, 1], ['ret', 'none']]] },
  { name: 'get' + tag, params: ['u32'], ret: tag, regs: [tag], blocks: [[['rload', 1, tag === 'i8' ? 0 : 1, 0], ['ret', 1]]] },
];
cases.signed = [{
  version: 7,
  regions: [mod('b', 'i8', 2), mod('h', 'i16', 2)],
  funcs: sg('i8').concat(sg('i16'), [
    // widen after load: a non-canonical (zero-extended) register would show here
    { name: 'wide8', params: ['u32'], ret: 'i64', regs: ['i8', 'i64'],
      blocks: [[['rload', 1, 0, 0], ['cast', 2, 1], ['ret', 2]]] },
    { name: 'wide16', params: ['u32'], ret: 'i64', regs: ['i16', 'i64'],
      blocks: [[['rload', 1, 1, 0], ['cast', 2, 1], ['ret', 2]]] },
  ]),
}, 'call seti8 0 -3\ncall seti8 1 127\ncall geti8 0\ncall geti8 1\ncall seti16 1 -32768\ncall geti16 1\n' +
  'call geti16 0\ncall wide8 0\ncall wide8 1\ncall wide16 1\nregion b\nregion h\n'];

const scripts = {
  rt_u8: 'call set 0 7\ncall set 3 255\ncall get 0\ncall get 3\ncall get 1\ncall get 4\ncall get -1\ncall set 4 1\nregion g\n',
  rt_i32: 'call set 0 -5\ncall set 2 2147483647\ncall get 0\ncall get 2\ncall get 3\ncall set 3 1\nregion g\n',
  rt_u64: 'call set 1 18446744073709551615\ncall get 1\ncall geti 1\ncall geti -1\ncall get 2\nregion g\n',
  rt_f32: 'call set 1 0x4079999a\ncall set 0 0xbf800000\ncall get 1\ncall get 0\ncall get 2\nregion g\n',
  rt_f64: 'call set 1 0x400f333333333333\ncall set 0 0x8000000000000000\ncall get 1\ncall get 0\ncall get -1\ncall get 2\nregion g\n',
  text_read: 'region t\nregion e\ncall tget 0\ncall tget 3\ncall tget 4\ncall eget 0\n',
  local_fresh: 'call swap 0 9\ncall swap 0 11\ncall swap 3 1\ncall swap 4 1\ncall reinit 0\ncall reinit 1\ncall reinit 2\n',
  persist: 'region c\ncall bump\ncall bump\ncall partial\nregion c\ncall ovf 5\ncall ovf 2000000000\nregion c\ncall bump\n',
  bitcast: calls('f32_u32', f('p39', 'nzero', 'inf')) + calls('u32_f32', ['1065353216', '0', '4286578688']) +
    calls('i32_f32', ['-1082130432', '1']) + calls('f64_i64', d('n39', 'nzero', 'm1')) +
    calls('u64_f64', ['4607182418800017408', '9223372036854775808']) + calls('u8_i8', ['255', '128', '127']) +
    calls('i8_u8', ['-1', '-128']) + calls('i16_u16', ['-2']) + calls('u16_i16', ['65535', '32768']) +
    calls('f32_f32', f('m1')),
  convert: calls('f64_i32', d('nan', 'inf', 'ninf', 'nzero', 'p39', 'n39', 'i32hi', 't31', 'i32lo', 'big', 'nbig')) +
    calls('f64_u8', d('m1', 'mhalf', 'u8hi', 't8', 'p39', 'nan', 'inf')) +
    calls('f32_i8', f('t7', 'n129', 'p127', 'n39', 'nan', 'ninf')) +
    calls('f64_u64', d('t64', 'u64hi', 't63', 'm1', 'nan', 'inf', 'big')) +
    calls('f64_i64', d('t63', 'nt63', 'i64hi', 'i64lo', 'nan', 'ninf', 'p25')) +
    calls('f32_u32', f('t32', 'u32hi', 'm1', 'nan', 'big', 't31')) +
    calls('f32_i16', f('i16hi', 'i16lo', 'nt31', 'tenth')) +
    calls('u64_f32', ['0', '18446744073709551615', '16777217', '9223372036854775808']) +
    calls('i32_f32', ['-2147483648', '16777217', '-1']) +
    calls('u64_f64', ['18446744073709551615', '9007199254740993', '0']) +
    calls('i64_f64', ['-9223372036854775808', '9223372036854775807', '-1']) +
    calls('f64_f32', d('tenth', 'big', 'nbig', 'nzero', 'p15', 'nan', 'inf')) +
    calls('f32_f64', f('tenth', 'big', 'nzero', 'ninf')) +
    calls('f64_f64', d('p39')) + calls('i32_u8', ['-1', '256', '255']) +
    calls('u64_i64', ['18446744073709551615', '9223372036854775807']),
  float_ops: 'call fadd 0x3fc00000 0x3dcccccd\ncall fadd 0x7f800000 0xff800000\ncall fmul 0x7149f2ca 0x7149f2ca\n' +
    'call fmul 0x80000000 0x447a0000\ncall dsub 0x400f333333333333 0x3fb999999999999a\n' +
    'call ddiv 0x3ff8000000000000 0x0000000000000000\ncall ddiv 0xbff0000000000000 0x0000000000000000\n' +
    'call fneg 0x447a0000\ncall fneg 0x00000000\ncall dneg 0x408f400000000000\n' +
    'call feq 0x00000000 0x80000000\ncall feq 0x7fc00000 0x7fc00000\ncall fne 0x7fc00000 0x7fc00000\n' +
    'call flt 0xbf800000 0x00000000\ncall flt 0x7fc00000 0x3fc00000\n' +
    'call dge 0x7ff8000000000000 0x3ff8000000000000\ncall dge 0x408f400000000000 0x408f400000000000\n',
  init_diamond: 'call pick 1\ncall pick 0\n',
  ok_loop_init_before: 'call f 5\ncall f 0\n',
  ok_own_local: 'call a 1\ncall b 1\n',
};

function main(argv) {
  if (argv.length !== 1) {
    process.stderr.write('usage: v7cases.js OUTDIR\n');
    return 2;
  }
  const dir = argv[0];
  fs.mkdirSync(dir, { recursive: true });
  let n = 0;
  for (const [name, [desc, script]] of Object.entries(cases)) {
    fs.writeFileSync(path.join(dir, name + '.unit'), encode(desc));
    fs.writeFileSync(path.join(dir, name + '.script'), script);
    n++;
  }
  for (const [name, script] of Object.entries(scripts)) {
    if (!fixtures[name]) throw new Error('v7cases: writer has no fixture ' + name);
    fs.writeFileSync(path.join(dir, name + '.unit'), encode(fixtures[name]));
    fs.writeFileSync(path.join(dir, name + '.script'), script);
    n++;
  }
  process.stdout.write(n + ' run cases\n');
  return 0;
}

process.exitCode = main(process.argv.slice(2));
