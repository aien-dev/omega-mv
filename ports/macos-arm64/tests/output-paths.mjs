// External host-wrapper regressions; all production artifacts are seed-built.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
const root = path.resolve(new URL('..', import.meta.url).pathname);
const compiler = '/home/drakestapleton/handoffs/omega-developer-gaps/shared/omc-191b776e';
const expectedHash = '191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9';
function run(cmd, args) {
  const r = spawnSync(cmd, args, { timeout: 90000, maxBuffer: 1024*1024 });
  assert.ifError(r.error); assert.equal(r.signal, null); return r;
}
function compile(source) {
  assert.equal(crypto.createHash('sha256').update(fs.readFileSync(compiler)).digest('hex'), expectedHash);
  const r = spawnSync('quiet-build', ['timeout', '60', compiler], { input: source, timeout: 90000 });
  assert.ifError(r.error); assert.equal(r.status, 0, r.stderr.toString());
  assert.ok(r.stdout.length >= 212); assert.equal(r.stdout.readUInt32LE(0), 0x464c457f);
  return r.stdout;
}
fs.mkdirSync(path.join(root, 'build'), {recursive:true});
const temp = fs.mkdtempSync(path.join(root, 'build', 'output-paths-'));
const modulePath = path.join(temp, 'module');fs.mkdirSync(modulePath);
for (const f of ['adapter.om','build.sh','target.sh']) fs.copyFileSync(path.join(root, f), path.join(modulePath, f));
const built=run('sh',[path.join(modulePath,'build.sh')]);assert.equal(built.status,0,built.stderr.toString());
const input=path.join(temp,'return42.elf');fs.writeFileSync(input,compile(fs.readFileSync(path.join(root,'tests','return42.om'))));
for (const kind of ['directory','symlink-directory']) {
  test(`target refuses ${kind} output without nesting artifacts`, () => {
    const dir=path.join(temp,'target-'+kind);fs.mkdirSync(dir);fs.writeFileSync(path.join(dir,'sentinel'),'unchanged');
    const out=kind==='directory'?dir:dir+'.link';if(out!==dir)fs.symlinkSync(dir,out);
    const r=run('sh',[path.join(modulePath,'target.sh'),input,out]);
    assert.equal(r.status,73,'directory output must fail rather than moving the artifact inside it');
    assert.deepEqual(fs.readdirSync(dir),['sentinel']);assert.equal(fs.readFileSync(path.join(dir,'sentinel'),'utf8'),'unchanged');
    assert.equal(fs.lstatSync(out).isSymbolicLink(),kind==='symlink-directory');
  });
  test(`build refuses ${kind} adapter destination without nesting artifacts`, () => {
    const copy=path.join(temp,'build-'+kind);fs.mkdirSync(copy);
    for(const f of ['adapter.om','build.sh'])fs.copyFileSync(path.join(root,f),path.join(copy,f));
    fs.mkdirSync(path.join(copy,'build'));const dir=path.join(copy,'preserved');fs.mkdirSync(dir);
    fs.writeFileSync(path.join(dir,'sentinel'),'unchanged');const out=path.join(copy,'build','adapter');
    if(kind==='directory')fs.renameSync(dir,out);else fs.symlinkSync(dir,out);
    const target=kind==='directory'?out:dir;
    const r=run('sh',[path.join(copy,'build.sh')]);
    assert.equal(r.status,73,'directory build destination must fail rather than report successful construction');
    assert.deepEqual(fs.readdirSync(target),['sentinel']);assert.equal(fs.readFileSync(path.join(target,'sentinel'),'utf8'),'unchanged');
    assert.equal(fs.lstatSync(out).isSymbolicLink(),kind==='symlink-directory');
  });
}
test('target still creates and replaces the requested regular file',()=>{
  const out=path.join(temp,'regular.macho');
  for(let i=0;i<2;i++){
    const r=run('sh',[path.join(modulePath,'target.sh'),input,out]);assert.equal(r.status,0,r.stderr.toString());
    assert.ok(fs.statSync(out).isFile());assert.equal(fs.readFileSync(out).readUInt32LE(0),0xfeedfacf);
  }
});
test('build still replaces the requested adapter with identical seed-built bytes',()=>{
  const out=path.join(modulePath,'build','adapter');const before=fs.readFileSync(out);
  const r=run('sh',[path.join(modulePath,'build.sh')]);assert.equal(r.status,0,r.stderr.toString());
  assert.deepEqual(fs.readFileSync(out),before);assert.ok(fs.statSync(out).isFile());
});
