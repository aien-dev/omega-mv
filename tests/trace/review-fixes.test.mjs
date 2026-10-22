import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fork} from 'node:child_process';
import {once} from 'node:events';
import {Archive,readArchive,recover} from '../../tools/trace/archive.mjs';
import {metric} from '../../tools/trace/schema.mjs';
import {query} from '../../tools/trace/query.mjs';
import {contribution} from '../../tools/trace/share.mjs';
import {main} from '../../tools/trace/trace.mjs';
import {fixture,temp,id} from './helpers.mjs';
const snapshot=root=>fs.readdirSync(root).sort().map(n=>[n,fs.readFileSync(path.join(root,n)).toString('hex')]);
async function child(t,mode,root){
 const c=fork(new URL('./crash-child.mjs',import.meta.url),[mode,root],{silent:true,
  execArgv:['--import',new URL('./crash-preload.mjs',import.meta.url).href],env:{...process.env,OMEGA_TRACE_TEST_CUT:mode}});
 t.after(()=>{if(c.exitCode===null&&c.signalCode===null)c.kill('SIGKILL');});
 let errors='';c.stderr.on('data',x=>errors+=x);
 const message=await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error('child cut timeout '+errors)),10000);
  c.once('message',m=>{clearTimeout(timer);resolve(m);});c.once('exit',code=>{clearTimeout(timer);reject(Error('child early exit '+code+' '+errors));});});
 return {c,message};
}
async function kill(c){const exited=once(c,'exit');assert.equal(c.kill('SIGKILL'),true);const [code,signal]=await exited;assert.equal(code,null);assert.equal(signal,'SIGKILL');}
test('T1 spans disclose every mandatory metadata path alongside selected metrics',async t=>{
 const root=path.join(temp(t),'a'),a=new Archive(root);a.append([fixture(123456,{producer:4321,sequence:'987654321',time:{domain:'logical',tick:'987654321',wall_ns:null},metrics:[metric('loss',0.5,'ratio','observed')]})]);a.close();
 const q=query(readArchive(root),{run:id(1)}),o={fields:['loss'],preset:'spans'},out=contribution(q,o),b=JSON.parse(out.bytes);
 const mandatory=['id','parent','run','session','producer','sequence','step','time.domain','time.tick','time.wall_ns','source','category','status','qualification','semantic','realization'].sort();
 assert.deepEqual(b.disclosures.privacy_surface.mandatory_span_fields,mandatory);
 const paths=Object.entries(b.spans[0]).filter(([k])=>k!=='metrics').flatMap(([k,v])=>v&&typeof v==='object'?Object.keys(v).map(x=>k+'.'+x):[k]).sort();
 assert.deepEqual(paths,mandatory);assert.deepEqual(b.disclosures.privacy_surface.numeric_fields,['loss']);
 assert.equal(b.spans[0].producer,4321);assert.equal(b.spans[0].sequence,'987654321');assert.equal(b.spans[0].step,123456);
 assert.match(b.disclosures.privacy_surface.span_metadata_notice,/workload structure/);
 const destination=root+'-spans.json',preview=await main(['--root',root,'share-preview',id(1),'--fields','loss','--preset','spans']);
 await main(['--root',root,'share-package',id(1),'--fields','loss','--preset','spans','--output',destination]);
 assert.equal(preview,out.bytes);assert.equal(fs.readFileSync(destination,'utf8'),out.bytes);
 assert.deepEqual(out,contribution(q,o));assert.throws(()=>contribution(q,{fields:['loss','producer'],preset:'spans'}));
});
test('T2 stale and active writer locks refuse packaging without guessing loss counts',t=>{
 const root=path.join(temp(t),'a'),a=new Archive(root);a.append([fixture()]);
 const active=readArchive(root);assert.equal(active.integrity,'INCONCLUSIVE');assert.match(active.problems.join(' '),/writer.*loss.*unknown/);
 assert.throws(()=>contribution(query(active,{run:id(1)}),{fields:['duration']}));a.close();assert.equal(readArchive(root).integrity,'PASS');
 fs.writeFileSync(path.join(root,'.writer.lock'),'dead-process',{mode:0o600});const before=snapshot(root),r=readArchive(root);
 assert.equal(r.integrity,'INCONCLUSIVE');assert.match(r.problems.join(' '),/writer.*loss.*unknown/);assert.deepEqual(snapshot(root),before);
 assert.throws(()=>contribution(query(r,{run:id(1)}),{fields:['duration']}));assert.throws(()=>new Archive(root));assert.deepEqual(snapshot(root),before);
 const destination=root+'-recovered';recover(root,destination);const recovered=readArchive(destination);
 assert.equal(recovered.integrity,'INCONCLUSIVE');const note=JSON.parse(fs.readFileSync(path.join(destination,'recovery.json')));assert.equal(note.loss_extent,'unknown');assert.equal(recovered.events.length,1);assert.equal(recovered.events.filter(e=>e.category==='loss').length,0);assert.deepEqual(snapshot(root),before);
});
for(const mode of ['startup','queued'])test(`T2 real killed child ${mode} has queued unacknowledged events and unknown loss`,async t=>{
 const root=path.join(temp(t),'a'),{c,message}=await child(t,mode,root);
 assert.equal(message.accepted,true);assert.equal(message.status.accepted_pending,1);assert.equal(message.status.persisted,0);
 // Active reader control, then actual abrupt process death at the same cut.
 assert.ok(fs.existsSync(root),'durable session marker before accepting enqueue');assert.equal(readArchive(root).integrity,'INCONCLUSIVE');await kill(c);
 const before=snapshot(root),r=readArchive(root);assert.equal(r.integrity,'INCONCLUSIVE');assert.match(r.problems.join(' '),/writer.*loss.*unknown/);
 assert.equal(r.events.length,0);assert.throws(()=>new Archive(root));assert.deepEqual(snapshot(root),before);
 const destination=root+'-recovered';recover(root,destination);assert.equal(readArchive(destination).integrity,'INCONCLUSIVE');
 assert.equal(JSON.parse(fs.readFileSync(path.join(destination,'recovery.json'))).loss_extent,'unknown');assert.deepEqual(snapshot(root),before);
});
test('T2 clean child close removes session markers only after durable acknowledgement',async t=>{
 const root=path.join(temp(t),'a'),{c,message}=await child(t,'clean',root);const exit=c.exitCode===null?await once(c,'exit'):[c.exitCode];assert.equal(exit[0],0);
 assert.equal(message.status.durable,true);assert.equal(message.status.accepted_pending,0);const r=readArchive(root);assert.equal(r.integrity,'PASS');assert.equal(r.events.length,1);
 assert.ok(contribution(query(r,{run:id(1)}),{fields:['duration']}).bytes);
 const a=new Archive(root);a.close();assert.equal(readArchive(root).integrity,'PASS');
});
test('T3 real purge cut after tombstone fsync diagnoses residual private bytes read-only',async t=>{
 const root=path.join(temp(t),'a'),{c,message}=await child(t,'purge',root);assert.equal(message.cut,'after_tombstone_fsync');await kill(c);
 // Remove only this synthetic child lock to isolate the independent tombstone finding.
 fs.unlinkSync(path.join(root,'.writer.lock'));const before=snapshot(root),r=readArchive(root);
 assert.equal(r.retention.length,1);assert.equal(r.events.length,0);assert.equal(r.integrity,'INCONCLUSIVE');
 assert.match(r.problems.join(' '),/00000001.seg.*tombstoned.*residual/);assert.ok(fs.existsSync(path.join(root,'00000001.seg')));
 query(r,{run:id(1)});assert.deepEqual(snapshot(root),before);assert.throws(()=>new Archive(root));assert.deepEqual(snapshot(root),before);
 recover(root,root+'-recovered');assert.deepEqual(snapshot(root),before);assert.equal(readArchive(root+'-recovered').events.length,0);
 assert.equal(readArchive(root+'-recovered').integrity,'INCONCLUSIVE');
});
test('T3 clean explicit purge fsyncs deletion and leaves a conclusive empty archive',t=>{
 const root=path.join(temp(t),'a'),a=new Archive(root);a.append([fixture()]);const sync=fs.fsyncSync;let durableDeletion=false;
 try{fs.fsyncSync=function(fd){sync(fd);if(fs.readlinkSync(`/proc/self/fd/${fd}`)===root&&!fs.existsSync(path.join(root,'00000001.seg')))durableDeletion=true;};
  assert.deepEqual(a.purge(),['00000001.seg']);
 }finally{fs.fsyncSync=sync;a.close();}
 assert.equal(durableDeletion,true,'explicit purge acknowledges directory fsync after unlink');
 const r=readArchive(root);assert.equal(r.integrity,'PASS');assert.equal(r.retention.length,1);assert.equal(r.events.length,0);assert.ok(!fs.existsSync(path.join(root,'00000001.seg')));
});
test('T2 reader racing clean shutdown retains opening writer uncertainty until next read',t=>{
 const root=path.join(temp(t),'a'),a=new Archive(root);a.append([fixture()]);
 const read=fs.readFileSync;let cut=false;
 try{fs.readFileSync=function(fd,...args){if(!cut&&typeof fd==='number'&&fs.readlinkSync(`/proc/self/fd/${fd}`).endsWith('/index.jsonl')){cut=true;a.close();}return read.call(this,fd,...args);};
  const r=readArchive(root);assert.equal(cut,true);assert.equal(r.integrity,'INCONCLUSIVE');assert.match(r.problems.join(' '),/writer.*loss.*unknown/);
 }finally{fs.readFileSync=read;a.close();}
 assert.equal(readArchive(root).integrity,'PASS');
});
test('T2 failed background persistence preserves unclean session marker even on close',async t=>{
 // Fixture exceeds requested segment bound; accepted enqueue cannot be persisted.
 const {BoundedWriter}=await import('../../tools/trace/archive.mjs');const root=path.join(temp(t),'a'),w=new BoundedWriter({root,enabled:true,segmentBytes:1024});
 t.after(async()=>{await w.close();});
 // Schema-valid receipt references make the frame exceed the segment bound.
 const e=fixture(1,{receipts:Array.from({length:16},(_,n)=>id(n+100))});assert.equal(w.submit(e),true);
 const status=await w.close();assert.equal(status.durable,false);assert.equal(status.persisted,0);assert.ok(status.errors.length);
 const r=readArchive(root);assert.equal(r.integrity,'INCONCLUSIVE');assert.match(r.problems.join(' '),/writer.*loss.*unknown/);
});
test('T2 adopted worker startup failure preserves reserved marker and unknown loss',async t=>{
 const root=path.join(temp(t),'a'),{c,message}=await child(t,'startup-failure',root);const exit=c.exitCode===null?await once(c,'exit'):[c.exitCode];assert.equal(exit[0],0);
 assert.equal(message.status.submitted,1);assert.equal(message.status.persisted,0);assert.equal(message.status.durable,false);
 assert.match(message.status.errors.join(' '),/EIO/);const r=readArchive(root);assert.equal(r.integrity,'INCONCLUSIVE');assert.match(r.problems.join(' '),/writer.*loss.*unknown/);
});
