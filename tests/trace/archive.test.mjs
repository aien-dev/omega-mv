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
 for(const change of [{schema:'v2'},{extra:'secret'},{metrics:[{name:'energy',value:1,unit:'ns',provenance:'observed',reason:null}]},{sequence:'-1'},{privacy:'public'},{event_id:id(9)}]) {const {event_id,...p}={...a,...change};assert.throws(()=>validate({...p,event_id:change.event_id??digest(p)}));}
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
 await w.close();assert.ok(accepted<=2);const r=readArchive(root);assert.equal(r.integrity,'PASS');
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
test('producer errors stay bounded, and durable losses retain producer and event class',async t=>{
 const root=path.join(temp(t),'a'),w=new BoundedWriter({root,enabled:true,capacity:1});w.submit(fixture());for(let n=0;n<100;n++){w.submit({...fixture(),privacy:'unclassified'});w.submit(fixture(n+2,{producer:n%2+1,source:'dual',category:'dual'}));}
 const status=await w.close();assert.ok(w.errors.length<=16);assert.ok(status.errors.length);const r=readArchive(root),losses=r.events.filter(e=>e.category==='loss');assert.ok(losses.length>=2);assert.ok(losses.every(e=>e.metrics.some(m=>m.name==='lost_source')&&e.metrics.some(m=>m.name==='lost_category')));assert.deepEqual([...new Set(losses.map(e=>e.producer))].sort(),[1,2]);
});
test('permission denial refuses writes; startup refusal and async persistence counts stay exact',async t=>{
 const p=temp(t),root=path.join(p,'a'),a=new Archive(root);a.append([fixture()]);a.close();fs.chmodSync(root,320);assert.throws(()=>new Archive(root),{code:'EACCES'});fs.chmodSync(root,448);
 const w=new BoundedWriter({root:path.join(p,'missing-parent','archive'),enabled:true,capacity:2});assert.equal(w.submit(fixture(2)),false);assert.ok(w.status().errors.length);const status=await w.close();assert.equal(status.submitted,0);assert.equal(status.accepted_pending,0);assert.equal(status.persisted,0);assert.equal(status.durable,false);assert.ok(status.errors.length);
 // Real asynchronous append failure after successful reservation: the next
 // queued message must not falsely acknowledge persistence after writer loss.
 const v=new BoundedWriter({root:path.join(p,'async'),enabled:true,capacity:2,segmentBytes:1024});
 t.after(async()=>{await v.close();});
 assert.equal(v.submit(fixture(3,{receipts:Array.from({length:16},(_,i)=>id(i+100))})),true);
 assert.equal(v.submit(fixture(4)),true);
 const failed=await v.close();assert.equal(failed.submitted,2);assert.equal(failed.persisted,0);assert.equal(failed.accepted_pending,0);assert.equal(failed.durable,false);assert.ok(failed.errors.length);
});
test('writer snapshots context and refuses cross-run/session mixing without throwing',async t=>{
 const root=path.join(temp(t),'a'),w=new BoundedWriter({root,enabled:true,capacity:1}),first=fixture();w.submit(first);first.status='refused';assert.equal(w.submit(fixture(2,{run:id(999)})),false);w.submit(fixture(3));const status=await w.close();assert.ok(status.errors.includes('writer context refused'));assert.equal(readArchive(root).events[0].status,'ok');
});
test('recovery does not resurrect retained-out segment bytes and preserves known loss',async t=>{
 const {recover}=await import('../../tools/trace/archive.mjs'),p=temp(t),root=path.join(p,'a'),dest=path.join(p,'b'),a=new Archive(root);a.append([fixture(1),fixture(2,{category:'loss',metrics:[metric('dropped',7,'events','observed')]})]);const file=fs.readdirSync(root).find(n=>n.endsWith('.seg')),saved=fs.readFileSync(path.join(root,file));a.purge();a.append([fixture(3)]);a.close();
 // Simulate interruption after durable retention tombstone but before deletion.
 fs.writeFileSync(path.join(root,file),saved,{mode:384});recover(root,dest);const r=readArchive(dest);assert.equal(r.events.filter(e=>e.category==='step').length,1);assert.equal(r.events.find(e=>e.category==='loss').metrics[0].value,7);assert.equal(r.integrity,'INCONCLUSIVE');
});
test('attribution overflow stays within a fixed table and reports all refused events',async t=>{
 const root=path.join(temp(t),'a'),w=new BoundedWriter({root,enabled:true,capacity:1});w.submit(fixture());for(let producer=0;producer<4100;producer++)w.submit(fixture(producer+2,{producer}));const status=await w.close();assert.equal(Object.keys(status.dropped).length,4097);assert.equal(status.dropped['65535:trace:loss'],4);assert.equal(readArchive(root).events.filter(e=>e.category==='loss').reduce((n,e)=>n+e.metrics.find(m=>m.name==='dropped').value,0),4100);assert.equal(status.durable,true);
});
test('drop-counter arithmetic overflow reports unknown rather than wrapping or throwing',async t=>{
 const root=path.join(temp(t),'a'),w=new BoundedWriter({root,enabled:true,capacity:1});w.submit(fixture());w.drops.set('1:train:step',Number.MAX_SAFE_INTEGER);let status;try{assert.equal(w.submit(fixture(2)),false);status=await w.close();}finally{await w.worker.terminate();}assert.equal(status.drop_count_overflow,true);assert.equal(readArchive(root).events.find(e=>e.category==='loss').metrics.find(m=>m.name==='dropped').value,null);
});
