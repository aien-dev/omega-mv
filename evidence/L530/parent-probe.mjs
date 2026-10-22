import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
const source=process.argv[2],base=process.argv[3];
fs.mkdirSync(base); // fresh owned fixtures only
const load=name=>import(pathToFileURL(path.join(source,'tools/trace',name)).href);
const {Archive,readArchive,recover}=await load('archive.mjs');
const {event,metric,canonical,hash}=await load('schema.mjs');
const {query}=await load('query.mjs');const {contribution}=await load('share.mjs');
const id=n=>n.toString(16).padStart(64,'0');
const e=event({run:id(1),session:id(2),producer:4321,sequence:'987654321',parent:null,time:{domain:'logical',tick:'987654321',wall_ns:null},contract:{machine:id(3),build:id(4),config:id(5),precision:'f32'},source:'train',category:'step',status:'ok',qualification:'synthetic',semantic:null,realization:null,step:123456,metrics:[metric('loss',0.5,'ratio','observed')],receipts:[]});
const snapshot=root=>Object.fromEntries(fs.readdirSync(root).sort().map(n=>[n,hash(fs.readFileSync(path.join(root,n)))]));
const root=path.join(base,'purge');let a=new Archive(root);a.append([e]);a.close();
let checks=0;
const bundle=JSON.parse(contribution(query(readArchive(root),{run:id(1)}),{fields:['loss'],preset:'spans'}).bytes);
for(const key of Object.keys(bundle.spans[0])){
 if(key==='metrics')continue;
 const keys=key==='time'?Object.keys(bundle.spans[0].time).map(k=>'time.'+k):[key];
 for(const k of keys){assert.ok(bundle.disclosures.privacy_surface.mandatory_span_fields.includes(k),k+' must be explicitly disclosed');checks++;}
}
assert.match(bundle.disclosures.privacy_surface.span_metadata_notice,/workload structure/);checks++;
const ix=path.join(root,'index.jsonl'),add=JSON.parse(fs.readFileSync(ix,'utf8').trim());
fs.appendFileSync(ix,canonical({schema:'omega.trace.index.v1',op:'remove',name:add.name,reason:'purge',digest:add.digest,losses:[]})+'\n');
const before=snapshot(root),r=readArchive(root);
assert.equal(r.integrity,'INCONCLUSIVE');checks++;
assert.ok(r.problems.some(s=>s.includes('tombstoned segment residual')));checks++;
assert.deepEqual(snapshot(root),before);checks++;
assert.ok(fs.existsSync(path.join(root,add.name)));checks++;
const crashRoot=path.join(base,'unclean');a=new Archive(crashRoot);a.append([e]);a.close();
fs.writeFileSync(path.join(crashRoot,'.writer.lock'),'dead-process',{mode:0o600});
const cb=snapshot(crashRoot),crash=readArchive(crashRoot);
assert.equal(crash.integrity,'INCONCLUSIVE');checks++;
assert.ok(crash.problems.some(s=>s.includes('loss extent unknown')));checks++;
assert.throws(()=>contribution(query(crash,{run:id(1)}),{fields:['loss']}));checks++;
recover(crashRoot,path.join(base,'recovered'));
assert.deepEqual(snapshot(crashRoot),cb);checks++;
assert.equal(readArchive(path.join(base,'recovered')).integrity,'INCONCLUSIVE');checks++;
console.log(JSON.stringify({parent_adversarial_checks:checks,findings:['T1 metadata disclosure','T2 unknown crash loss and packaging refusal','T3 residual purge diagnosis'],original_sources_read_only:true,qualification:'SYNTHETIC_HOST_ONLY'}));
