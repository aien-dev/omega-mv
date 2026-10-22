import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import {Archive,readArchive} from '../../tools/trace/archive.mjs';import {query} from '../../tools/trace/query.mjs';import {canonical,hash,metric,event} from '../../tools/trace/schema.mjs';import {contribution} from '../../tools/trace/share.mjs';import {main} from '../../tools/trace/trace.mjs';import {fixture,temp,id} from './helpers.mjs';
function setup(t){const p=temp(t),root=path.join(p,'a'),a=new Archive(root);a.append([fixture(1),fixture(2,{metrics:[metric('duration',4,'ns','observed'),metric('energy',null,'J','unknown','not_recorded')]}),fixture(3,{category:'loss',metrics:[metric('dropped',7,'events','observed')]})]);a.close();return {p,root,q:query(readArchive(root),{run:id(1)})};}
test('deterministic golden contribution, exact preview/package bytes, explicit allowlist and local-only consent',async t=>{
 const {p,root,q}=setup(t),options={fields:['duration'],preset:'summary'},a=contribution(q,options),b=contribution(q,options);assert.deepEqual(a,b);assert.equal(a.bytes,fs.readFileSync(new URL('./golden-contribution.json',import.meta.url),'utf8'));assert.equal(hash(a.bytes),a.digest);const value=JSON.parse(a.bytes);assert.equal(value.training_permission,false);assert.equal(value.transfer_permission,false);assert.equal(value.schema,'omega.trace.contribution.v1');assert.equal(value.disclosures.dropped[0].metric.value,7);
 const output=path.join(p,'package.json'),preview=await main(['--root',root,'share-preview',id(1),'--fields','duration']);await main(['--root',root,'share-package',id(1),'--fields','duration','--output',output]);assert.equal(fs.readFileSync(output,'utf8'),preview);assert.equal(preview,a.bytes);
 assert.match(value.content_hash,/^[a-f0-9]{64}$/);assert.equal(value.content_hash,hash(canonical(Object.fromEntries(Object.entries(value).filter(([k])=>k!=='content_hash')))));
});
test('canary prompts credentials samples paths tensors never enter schema or standard bundles',t=>{
 const {q}=setup(t);for(const field of ['prompt','credential','sample','home','tensor','metadata']){const canary='/home/private/API_TOKEN_training_string_tensor_0000';const e={...q.events[0],[field]:canary};const mutated={...q,events:[e]};assert.throws(()=>contribution(mutated,{fields:['duration']}));}
 const bytes=contribution(q,{fields:['duration']}).bytes;for(const text of ['/home/','API_TOKEN','training_string','tensor_0000',id(1),id(2)])assert.ok(!bytes.includes(text));
 for(const fields of [[],['prompt'],['duration','unknown'],['__proto__']])assert.throws(()=>contribution(q,{fields}));
 assert.throws(()=>contribution(q,{fields:['duration'],preset:'unknown'}));
});
test('unknown schema, unverified provenance, truncation, dangling parent and source mutation refuse packaging',t=>{
 const {root,q}=setup(t),before=fs.readdirSync(root).map(n=>fs.readFileSync(path.join(root,n)).toString('hex'));
 assert.throws(()=>contribution({...q,schema:'v9'},{fields:['duration']}));assert.throws(()=>contribution({...q,integrity:'INCONCLUSIVE'},{fields:['duration']}));assert.throws(()=>contribution({...q,source_segments:[]},{fields:['duration']}));
 contribution(q,{fields:['duration'],preset:'spans'});assert.deepEqual(before,fs.readdirSync(root).map(n=>fs.readFileSync(path.join(root,n)).toString('hex')));
 const n=fs.readdirSync(root).find(n=>n.endsWith('.seg'));fs.truncateSync(path.join(root,n),11);assert.throws(()=>contribution(query(readArchive(root),{run:id(1)}),{fields:['duration']}));
});
test('CLI rejects symlink/traversal source and refuses writing source archive or existing output',async t=>{
 const {root,p}=setup(t);fs.symlinkSync(root,path.join(p,'link'));await assert.rejects(()=>main(['--root',path.join(p,'link'),'share-preview',id(1),'--fields','duration']));
 await assert.rejects(()=>main(['--root',root+'/../a','share-preview',id(1),'--fields','duration']));
 await assert.rejects(()=>main(['--root',root,'share-package',id(1),'--fields','duration','--output',path.join(root,'export.json')]));
 const out=path.join(p,'existing');fs.writeFileSync(out,'preserve',{mode:384});await assert.rejects(()=>main(['--root',root,'share-package',id(1),'--fields','duration','--output',out]));assert.equal(fs.readFileSync(out,'utf8'),'preserve');
});
test('top-level disclosure metadata cannot carry unchecked private strings',t=>{
 const {q}=setup(t);assert.throws(()=>contribution({...q,coverage:'/home/private/CANARY'},{fields:['duration']}));assert.throws(()=>contribution({...q,metadata:'CANARY'},{fields:['duration']}));
});
test('step/status filters and retention cannot conceal known run loss counts',t=>{
 const {root}=setup(t),selected=query(readArchive(root),{run:id(1),type:'step',status:'ok'}),b=JSON.parse(contribution(selected,{fields:['duration']}).bytes);assert.equal(b.disclosures.dropped.length,1);assert.equal(b.disclosures.dropped[0].metric.value,7);
 const a=new Archive(root);a.append([fixture(9,{category:'loss',metrics:[metric('dropped',11,'events','observed')]})]);a.retain({bytes:0,olderThanMs:Infinity});a.append([fixture(10)]);a.close();
 const retained=query(readArchive(root),{run:id(1)}),out=JSON.parse(contribution(retained,{fields:['duration']}).bytes);assert.equal(out.disclosures.dropped.reduce((n,d)=>n+d.metric.value,0),18);assert.equal(out.disclosures.retained_out_segments,2);
});
test('unknown binary/metadata fields in an integrity-correct segment refuse local packaging',t=>{
 const {root}=setup(t),n=fs.readdirSync(root).find(n=>n.endsWith('.seg')),file=path.join(root,n),b=fs.readFileSync(file);const e=JSON.parse(b.subarray(44,44+b.readUInt32BE(8)).toString('utf8'));
 const {event_id,...payload}=e;payload.metadata={prompt:'CANARY_PRIVATE_PROMPT',credential:'CANARY_TOKEN',tensor:Buffer.from('CANARY_TENSOR').toString('hex')};const bytes=Buffer.from(canonical({...payload,event_id:hash(canonical(payload))})),h=Buffer.alloc(36);h.writeUInt32BE(bytes.length);Buffer.from(hash(bytes),'hex').copy(h,4);const changed=Buffer.concat([b.subarray(0,8),h,bytes]);fs.writeFileSync(file,changed);
 const indexPath=path.join(root,'index.jsonl'),entry=JSON.parse(fs.readFileSync(indexPath,'utf8').trim());entry.bytes=changed.length;entry.digest=hash(changed);entry.records=1;fs.writeFileSync(indexPath,canonical(entry)+'\n');const q=query(readArchive(root),{run:id(1)});assert.equal(q.integrity,'INCONCLUSIVE');assert.throws(()=>contribution(q,{fields:['duration']}));
});
