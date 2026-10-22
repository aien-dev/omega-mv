// Public source-closure verification oracle; Node is not an install dependency.
import fs from 'node:fs';
import crypto from 'node:crypto';
import zlib from 'node:zlib';
import assert from 'node:assert/strict';
const base = new URL('../', import.meta.url);
const manifest = JSON.parse(fs.readFileSync(process.argv[3] || new URL('SOURCE.json', base)));
const compressed = fs.readFileSync(process.argv[2] || new URL(manifest.archive, base));
const hash = (algorithm, bytes) => crypto.createHash(algorithm).update(bytes).digest('hex');
const gitHash = (type, bytes) => hash('sha1', Buffer.concat([Buffer.from(`${type} ${bytes.length}\0`), bytes]));
assert.equal(hash('sha256', compressed), manifest.sha256, 'archive digest');
assert.equal(compressed.length, manifest.bytes, 'archive length');
const tar = zlib.gunzipSync(compressed), files = new Map(), directories = new Set();
for (let pos = 0; pos < tar.length;) {
  const header = tar.subarray(pos, pos + 512);
  if (header.every(b => b === 0)) break;
  const field = (a, b) => header.subarray(a, b).toString().replace(/\0.*$/s, '');
  const name = field(0, 100), size = parseInt(field(124, 136).trim() || '0', 8);
  const type = field(156, 157), bytes = tar.subarray(pos + 512, pos + 512 + size);
  const mode = parseInt(field(100, 108).trim(), 8);
  pos += 512 + Math.ceil(size / 512) * 512;
  if (type === 'g') continue; // git's global archive metadata
  assert(name.startsWith('omega/') && !name.split('/').includes('..'), 'safe archive path');
  if (type === '5') { assert(!directories.has(name)); directories.add(name); continue; }
  assert(type === '' || type === '0', 'regular source files only');
  const relative = name.slice(6);
  assert(relative && !files.has(relative), 'unique member');
  assert(!bytes.includes(0), 'textual source only');
  assert.equal(mode & 0o170000, 0, 'tar mode has no type flags');
  assert([0o664, 0o775, 0o644, 0o755].includes(mode), 'source file mode');
  files.set(relative, { mode: mode & 0o111 ? '100755' : '100644', oid: gitHash('blob', bytes) });
  if (relative === 'LICENSE') assert.equal(hash('sha256', bytes), manifest.license_sha256, 'license');
}
assert(files.has('LICENSE') && files.has('provenance/OMEGA-LICENSE'), 'complete licenses');
assert.equal(files.size, manifest.source_files, 'complete source count');
function tree(prefix) {
  const children = new Map();
  for (const [path, file] of files) {
    if (!path.startsWith(prefix)) continue;
    const tail = path.slice(prefix.length), slash = tail.indexOf('/');
    if (slash < 0) children.set(tail, file);
    else children.set(tail.slice(0, slash), { mode: '40000' });
  }
  const entries = [...children].sort(([a, x], [b, y]) => Buffer.compare(Buffer.from(a + (x.mode === '40000' ? '/' : '')), Buffer.from(b + (y.mode === '40000' ? '/' : ''))));
  return gitHash('tree', Buffer.concat(entries.map(([name, file]) => Buffer.concat([
    Buffer.from(`${file.mode} ${name}\0`), Buffer.from(file.mode === '40000' ? tree(prefix + name + '/') : file.oid, 'hex')
  ]))));
}
assert.equal(tree(''), manifest.source_tree, 'complete source git tree identity');
console.log(`PASS archive SHA256, ${files.size} source blobs and modes, complete git tree, licenses; ${directories.size} directories`);
