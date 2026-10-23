// Independent byte/format oracle. Reads artifacts; never constructs them.
import fs from 'node:fs';
import assert from 'node:assert/strict';
export function checkMacho(b, input, hello = false) {
  const u32 = p => b.readUInt32LE(p);
  const u64 = p => Number(b.readBigUInt64LE(p));
  const name = (p, n) => b.subarray(p, p + n).toString().split('\0')[0];
  assert.ok(b.length > 0, 'nonempty artifact');
  assert.equal(u32(0), 0xfeedfacf, 'native Mach-O boundary');
  assert.equal(u32(4), 0x0100000c, 'ARM64 CPU');
  assert.equal(u32(8), 0, 'ARM64_ALL subtype');
  assert.equal(u32(12), 2, 'executable');
  assert.equal(u32(24), 0x200085, 'PIE + dyld + two-level + no undefined');
  assert.equal(u32(28), 0, 'reserved');
  assert.equal(b.length, 32784, 'bounded complete output');
  assert.equal(u32(16), 11);
  const commands = new Map();
  let p = 32;
  for (let i = 0; i < u32(16); i++) {
    const cmd = u32(p), size = u32(p + 4);
    assert.ok(size >= 8 && size % 8 === 0 && p + size <= 32 + u32(20));
    const key = cmd === 0x19 ? name(p + 8, 16) : cmd;
    assert.ok(!commands.has(key), 'unique commands');
    commands.set(key, p);
    p += size;
  }
  assert.equal(p, 32 + u32(20));
  assert.equal(p, 640);
  p = commands.get('__PAGEZERO');
  assert.equal(u64(p + 24), 0); assert.equal(u64(p + 32), 2 ** 32);
  assert.equal(u64(p + 48), 0); assert.equal(u32(p + 56), 0); assert.equal(u32(p + 60), 0);
  p = commands.get('__TEXT');
  assert.equal(u64(p + 24), 2 ** 32); assert.equal(u64(p + 32), 32768);
  assert.equal(u64(p + 40), 0); assert.equal(u64(p + 48), 32768);
  assert.equal(u32(p + 56), 5); assert.equal(u32(p + 60), 5);
  assert.equal(u32(p + 64), 1);
  const sec = p + 72;
  assert.equal(name(sec, 16), '__text'); assert.equal(name(sec + 16, 16), '__TEXT');
  assert.equal(u64(sec + 32), 2 ** 32 + 16384);
  assert.equal(u32(sec + 48), 16384); assert.equal(u32(sec + 52), 2);
  assert.equal(u32(sec + 56), 0); assert.equal(u32(sec + 60), 0, 'no relocations');
  assert.equal(u32(sec + 64), 0x80000400);
  p = commands.get('__LINKEDIT');
  assert.equal(u64(p + 24), 2 ** 32 + 32768); assert.equal(u64(p + 32), 16384);
  assert.equal(u64(p + 40), 32768); assert.equal(u64(p + 48), 16);
  assert.equal(u32(p + 56), 1); assert.equal(u32(p + 60), 1);
  p = commands.get(0xe);
  assert.equal(name(p + u32(p + 8), u32(p + 4) - u32(p + 8)), '/usr/lib/dyld');
  p = commands.get(0xc);
  assert.equal(name(p + u32(p + 8), u32(p + 4) - u32(p + 8)), '/usr/lib/libSystem.B.dylib');
  p = commands.get(0x32);
  assert.equal(u32(p + 8), 1); assert.equal(u32(p + 12), 0xb0000);
  assert.equal(u32(p + 16), 0xb0000); assert.equal(u32(p + 20), 0);
  assert.ok(commands.has(0x1b), 'UUID for signing');
  assert.ok(commands.has(0x80000022), 'dyld info');
  assert.ok(commands.has(2) && commands.has(0xb), 'symbol tables');
  if (!input) return;
  const n = input.length, entry = n - 24, offset = a => 16384 + a - 176;
  assert.equal(u64(commands.get(0x80000028) + 8), offset(entry), 'entry points to bootstrap');
  assert.equal(u64(sec + 40), n - 176 + 16);
  // Independent expected semantic edits. All other bytes must remain unchanged.
  const changes = new Map([
    [entry, 0xd280001c], [entry + 4, 0xd503201f], [entry + 8, 0xd503201f],
  ]);
  for (let a = 180; a < n; a += 4) {
    if (input.readUInt32LE(a) === 0xd2800ba8) {
      changes.set(a, 0xd2800030); changes.set(a + 4, 0xd4001001);
    }
  }
  if (hello) {
    changes.set(264, 0x10fffd61); changes.set(268, 0xd503201f);
    changes.set(284, 0x94000000 + (n - 284) / 4); changes.set(288, 0xd503201f);
    assert.equal(b.subarray(offset(180), offset(196)).toString(), 'hello omega mac\n');
  }
  for (let a = 176; a < n; a += 4)
    assert.equal(u32(offset(a)), changes.get(a) ?? input.readUInt32LE(a), `preserved/adapted word at input ${a}`);
  const thunk = offset(n);
  const tail = hello ? [0xd2800090, 0xd4001001, 0xda803400, 0xd65f03c0] : [0, 0, 0, 0];
  tail.forEach((w, i) => assert.equal(u32(thunk + i * 4), w, 'Darwin write/error thunk'));
  assert.ok(b.subarray(640, 16384).every(x => x === 0), 'header padding');
  assert.ok(b.subarray(thunk + 16).every(x => x === 0), 'bounded zero padding and LINKEDIT');
}
if (process.argv[1] === new URL(import.meta.url).pathname) {
  const input = process.argv[3] ? fs.readFileSync(process.argv[3]) : undefined;
  checkMacho(fs.readFileSync(process.argv[2]), input, process.argv[4] === 'hello');
  console.log('PASS independent Mach-O ARM64 bytes, format, entry, ABI and syscall edits');
}
