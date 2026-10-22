// Independent format oracle only. Never constructs target artifacts.
import fs from 'node:fs';
import assert from 'node:assert/strict';
const b = fs.readFileSync(process.argv[2]);
assert.ok(b.length > 0, 'nonempty artifact');
assert.equal(b.readUInt32LE(0), 0xfeedfacf, 'native Mach-O boundary');
assert.equal(b.readUInt32LE(4), 0x0100000c, 'ARM64 CPU');
assert.equal(b.readUInt32LE(12), 2, 'executable');
console.log('PASS Mach-O ARM64 executable boundary');
