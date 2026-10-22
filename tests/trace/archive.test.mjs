import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { canonical, digest, event, metric, validate } from '../../tools/trace/schema.mjs';
import { Archive, readArchive, BoundedWriter } from '../../tools/trace/archive.mjs';
import {id,fixture,temp} from './helpers.mjs';
test('canonical bytes independent of key insertion; strict types/units/versions',()=>{
 const a=fixture(); assert.equal(canonical(a),canonical(JSON.parse(canonical(a))));
 assert.equal(digest(a),digest({...a}));
 for(const change of [{schema:'v2'},{extra:'secret'},{metrics:[{name:'energy',value:1,unit:'ns',provenance:'observed',reason:null}]},{sequence:'-1'},{privacy:'public'},{event_id:id(9)}]) assert.throws(()=>validate({...a,...change}));
 assert.throws(()=>metric('duration',NaN,'ns','observed'));
});
test('sealed archive reopen, deterministic payloads, mode, no source mutation',t=>{
 const p=temp(t), root=path.join(p,'archive'), a=new Archive(root,{segmentBytes:1500});
 a.append([fixture(1),fixture(2),fixture(3)]);a.close();
 const before=fs.readdirSync(root).map(n=>[n,fs.readFileSync(path.join(root,n)).toString('hex')]);
 const r=readArchive(root); assert.equal(r.integrity,'PASS'); assert.equal(r.events.length,3);
 assert.equal(fs.statSync(root).mode&511,448);
 for(const n of fs.readdirSync(root))assert.equal(fs.statSync(path.join(root,n)).mode&511,384);
 assert.deepEqual(before,fs.readdirSync(root).map(n=>[n,fs.readFileSync(path.join(root,n)).toString('hex')]));
 const b=new Archive(root);b.append([fixture(4)]);b.close();assert.equal(readArchive(root).events.length,4);
});
test('truncation, corrupt length/version/index and orphan fail visibly; recover preserves original',t=>{
 for(const mode of ['truncate','length','version','index','orphan']){
 const root=path.join(temp(t),mode),a=new Archive(root);a.append([fixture(1),fixture(2)]);a.close();
 const f=path.join(root,fs.readdirSync(root).find(n=>n.endsWith('.seg'))),buf=fs.readFileSync(f);
 if(mode==='truncate')fs.truncateSync(f,buf.length-5);
 if(mode==='length'){buf.writeUInt32BE(0xffffffff,8);fs.writeFileSync(f,buf);}
 if(mode==='version'){buf[6]=9;fs.writeFileSync(f,buf);}
 if(mode==='index')fs.appendFileSync(path.join(root,'index.jsonl'),'bad\n');
 if(mode==='orphan')fs.writeFileSync(path.join(root,'00000099.seg'),buf,{mode:384});
 const before=fs.readFileSync(f);const r=readArchive(root);assert.equal(r.integrity,'INCONCLUSIVE',mode);assert.ok(r.problems.length);
 assert.deepEqual(before,fs.readFileSync(f));
 }
});
test('symlinks, unsafe permissions, swaps and hardlinks refused',t=>{
 const p=temp(t),root=path.join(p,'a');const a=new Archive(root);a.append([fixture()]);a.close();
 fs.symlinkSync(root,path.join(p,'link'));assert.throws(()=>readArchive(path.join(p,'link')));
 fs.chmodSync(root,511);assert.throws(()=>readArchive(root));fs.chmodSync(root,448);
 const n=fs.readdirSync(root).find(n=>n.endsWith('.seg'));fs.renameSync(path.join(root,n),path.join(p,'saved'));fs.symlinkSync(path.join(p,'saved'),path.join(root,n));assert.throws(()=>readArchive(root));
 const b=new Archive(path.join(p,'b'));fs.renameSync(path.join(p,'b'),path.join(p,'moved'));fs.symlinkSync(path.join(p,'moved'),path.join(p,'b'));assert.throws(()=>b.append([fixture()]));b.close();
});
test('quota, age, pin and explicit purge preserve unrelated receipts',t=>{
 const p=temp(t),root=path.join(p,'a'),a=new Archive(root);fs.writeFileSync(path.join(p,'cortex.receipt'),'untouched');
 a.append([fixture(1)]);a.append([fixture(2)]);const names=readArchive(root).segments.map(s=>s.name);
 a.pin(names[0],true);a.retain({bytes:0,olderThanMs:Infinity});assert.equal(readArchive(root).events.length,1);
 assert.ok(readArchive(root).retention.length);a.purge();assert.equal(readArchive(root).events.length,1);
 a.pin(names[0],false);a.purge();assert.equal(readArchive(root).events.length,0);a.close();
 assert.equal(fs.readFileSync(path.join(p,'cortex.receipt'),'utf8'),'untouched');
});
test('disabled writer no files; bounded burst durable per-class loss',async t=>{
 const root=path.join(temp(t),'a'),off=new BoundedWriter({root});assert.equal(off.submit(fixture()),false);await off.close();assert.equal(fs.existsSync(root),false);
 const w=new BoundedWriter({root,enabled:true,capacity:2});
 let accepted=0;for(let n=1;n<=100;n++)accepted+=Number(w.submit(fixture(n)));
 assert.ok(accepted<=2);await w.close();const r=readArchive(root);assert.equal(r.integrity,'PASS');
 assert.equal(r.events.filter(e=>e.category==='step').length,accepted);
 assert.equal(r.events.filter(e=>e.category==='loss').reduce((s,e)=>s+e.metrics.find(m=>m.name==='dropped').value,0),100-accepted);
});
test('independent frame oracle verifies exact bytes, endian, digest, and payload',async t=>{
 const {createHash}=await import('node:crypto'),{frame,scan}=await import('../../tools/trace/archive.mjs');
 const e=fixture(7),b=frame(e),p=Buffer.from(canonical(e));assert.equal(b.readUInt32BE(0),p.length);assert.deepEqual(b.subarray(36),p);
 assert.equal(b.subarray(4,36).toString('hex'),createHash('sha256').update(p).digest('hex'));
 const manual=Buffer.concat([Buffer.from('OTRACE1\n'),b]);assert.deepEqual(scan(manual).events,[e]);
 for(const offset of [0,8,12,manual.length-1]){const mutant=Buffer.from(manual);mutant[offset]^=1;assert.ok(scan(mutant).problems.length);}
});
test('unclean shutdown and recovery into fresh archive preserve every complete orphan frame',async t=>{
 const {recover,frame}=await import('../../tools/trace/archive.mjs');const p=temp(t),source=path.join(p,'a'),dest=path.join(p,'b');
 const a=new Archive(source);a.append([fixture(1)]);a.close();
 const orphan=Buffer.concat([Buffer.from('OTRACE1\n'),frame(fixture(2)),frame(fixture(3))]);
 fs.writeFileSync(path.join(source,'00000009.seg'),orphan.subarray(0,orphan.length-7),{mode:384});
 fs.writeFileSync(path.join(source,'.writer.lock'),'dead-writer',{mode:384});
 const before=fs.readFileSync(path.join(source,'00000009.seg'));const result=recover(source,dest);
 assert.equal(result.integrity,'INCONCLUSIVE');assert.equal(result.recovered,2);assert.equal(readArchive(dest).events.length,2);
 assert.deepEqual(before,fs.readFileSync(path.join(source,'00000009.seg')));
});
test('write failure is explicit, no empty-success index; existing writer cannot be displaced',t=>{
 const root=path.join(temp(t),'a'),a=new Archive(root);assert.throws(()=>new Archive(root));
 const original=fs.writeSync;try{fs.writeSync=()=>{const e=new Error('synthetic full disk');e.code='ENOSPC';throw e;};assert.throws(()=>a.append([fixture()]),{code:'ENOSPC'});}finally{fs.writeSync=original;a.close();}
 assert.equal(readArchive(root).integrity,'INCONCLUSIVE');
});
test('parent-component symlink, group-readable file, and hardlink refused',t=>{
 const p=temp(t),root=path.join(p,'a'),a=new Archive(root);a.append([fixture()]);a.close();
 fs.symlinkSync(p,path.join(p,'parent-link'));assert.throws(()=>readArchive(path.join(p,'parent-link','a')));
 fs.chmodSync(path.join(root,'index.jsonl'),420);assert.throws(()=>readArchive(root));fs.chmodSync(path.join(root,'index.jsonl'),384);
 fs.linkSync(path.join(root,'index.jsonl'),path.join(p,'hard'));assert.throws(()=>readArchive(root));
});
