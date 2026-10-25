// Source-aware v2 compatibility gate (spec/universal/source-aware-v2.md). Node is orchestration and
// independent expectation only; every decision under test runs in the native seeded Omega codecs.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync,execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {hash,unit,op,u32,expected} from './fixtures.mjs';
import {inspect,legality} from './reference.mjs';
import {valid,damaged,categories} from './corpus.mjs';

const V1_CORE_SHA256='24c0837d05990fcb1daaafec13d2408d4359ae4fa6c9866307b73c5132c8b7eb';
const FROZEN_SA_SHA256='1a127bf032e403b103d12d3140f27e74ab490e9a8b7af46b70233c6f3aaa60ce';
const COMPILER_SHA256='191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9';
const root='build/L832',run=path.join(root,'runs',new Date().toISOString().replaceAll(':','-'));
const FROZEN=process.env.SA_FROZEN??'build/L574/sa',NEW=path.join(root,'sa'),compiler=path.join(root,'toolchain/build/bootstrap/omc');
const CONC=Number(process.env.SA_V2_JOBS??6);
fs.mkdirSync(run,{recursive:true});fs.rmSync(path.join(root,'PASS.json'),{force:true});
const digest=b=>hash(b).toString('hex'),filehash=p=>digest(fs.readFileSync(p));
const report={schema:'omega.source-aware.v2-compat.development.v1',fidelity:'DIFFERENTIAL_COMPATIBILITY',host:'HOST_NATIVE',run,sourceHEAD:execFileSync('git',['rev-parse','HEAD']).toString().trim(),review:'PENDING (#623 independent v2 reference)',signed:false,
 notRun:['independent v2 reference decoder (#623)','v2 adoption by any consumer','Python adapters on universal/627-python-u64','GPU/hardware targets'],steps:[]};

// ---------- process helpers ----------
// One request per process for both codecs. Node's fork-based spawn costs about 2 ms per process here,
// so a small Python posix_spawn runner executes the job file (orchestration only, no codec logic).
const RUNNER=`import struct,subprocess,sys
from multiprocessing import Pool
src,dst,jobs=sys.argv[1],sys.argv[2],int(sys.argv[3]);exes=sys.argv[4:]
def go(chunk):
    out=[]
    for e,m,inp in chunk:
        r=subprocess.run([exes[e]],input=bytes([m])+inp,capture_output=True,close_fds=False)
        st=r.returncode if r.returncode>=0 else 1000-r.returncode
        out.append(struct.pack('<HI',st,len(r.stdout))+r.stdout)
    return b''.join(out)
data=open(src,'rb').read();items=[];p=0
while p<len(data):
    e,m,n=struct.unpack_from('<BBI',data,p);p+=6;items.append((e,m,data[p:p+n]));p+=n
with Pool(jobs) as pool, open(dst,'wb') as f:
    for part in pool.imap(go,[items[i:i+256] for i in range(0,len(items),256)]):f.write(part)
`;
let spawned=0;
function runAll(jobs){const exes=[...new Set(jobs.map(j=>j[0]))],idx=new Map(exes.map((e,i)=>[e,i])),parts=[];
 for(const [e,m,input] of jobs){const h=Buffer.alloc(6);h[0]=idx.get(e);h[1]=m;h.writeUInt32LE(input.length,2);parts.push(h,input);}
 const src=path.join(run,'jobs.bin'),dst=path.join(run,'results.bin');fs.writeFileSync(src,Buffer.concat(parts));
 const r=spawnSync('python3',['-c',RUNNER,src,dst,String(CONC),...exes],{stdio:['ignore','inherit','inherit']});assert.equal(r.status,0,'process runner');
 const out=fs.readFileSync(dst),res=[];let p=0;for(let i=0;i<jobs.length;i++){const status=out.readUInt16LE(p),n=out.readUInt32LE(p+2);p+=6;res.push({status,stdout:out.subarray(p,p+n)});p+=n;}
 assert.equal(p,out.length,'runner result framing');fs.rmSync(src);fs.rmSync(dst);spawned+=jobs.length;return res;}
function compile(source,name){const exe=path.join(run,name);fs.writeFileSync(exe+'.om',source);const r=spawnSync(compiler,[],{input:Buffer.from(source),maxBuffer:64<<20});
 assert.equal(r.status,0,`${name} compilation: ${r.stderr}`);fs.writeFileSync(exe,r.stdout,{mode:0o700});
 assert.equal(spawnSync('sh',[path.join(root,'toolchain/tools/verify-elf.sh'),exe]).status,0,'AArch64 ELF required');return {exe,sourceSHA256:digest(Buffer.from(source)),binarySHA256:digest(r.stdout)};}
function batch(exe,requests){
 const input=Buffer.concat(requests.flatMap(r=>[Buffer.of(r.mode),u32(r.input.length),r.input]));
 const r=spawnSync(exe,[],{input,maxBuffer:1<<30,timeout:600000});let out=r.stdout,pos=0,rows=[];
 for(const q of requests){if(pos+37>out.length)break;const code=out[pos],len=out.readUInt32LE(pos+1);
  assert.ok(out.subarray(pos+5,pos+37).equals(hash(Buffer.concat([Buffer.of(q.mode),q.input]))),'consumed input mismatch');pos+=37;rows.push({status:code,stdout:out.subarray(pos,pos+len)});pos+=len;}
 return {status:r.status,rows,complete:rows.length===requests.length&&pos===out.length};}
const same=(a,b)=>a.status===b.status&&a.stdout.equals(b.stdout);

// ---------- unit construction (test-only, independent of the codec) ----------
const LANG={1:1,2:2,3:3,4:3,5:4,6:3,7:5,8:6,9:7};
const semLen=b=>b.readUInt32LE(5);
function rehash(b){const sn=semLen(b),pn=b.readUInt32LE(9);if(pn>=42&&b.length>=13+sn+42)hash(b.subarray(13,13+sn)).copy(b,13+sn+10);return b;}
function header(u,{version,lang,profile,runtime,abi,dialect}){const b=Buffer.from(u);
 if(version!==undefined)b[17]=version;if(lang!==undefined)b[18]=lang;if(profile!==undefined)b[19]=profile;if(runtime!==undefined)b[21]=runtime;if(abi!==undefined)b[22]=abi;
 const d=dialect??lang;if(d!==undefined)for(let i=0;i<b.readUInt16LE(23);i++)b[57+20*i]=d;return rehash(b);}
const retarget=(u,profile,version=2)=>header(u,{version,lang:LANG[profile],profile});
const envelopeOf=(mode,input)=>mode===6?input.subarray(4):input;
const byte17=(mode,input)=>{const e=envelopeOf(mode,input);return e.length>17?e[17]:-1;};
const CAPS=[];for(const t of [1,2])for(const r of [0,1,2])for(const a of [1,2])for(const e of [0,17,63])CAPS.push(Buffer.of(t,r,a,e));
const BADCAPS=[[0,0,1,0],[3,0,1,0],[1,3,1,0],[1,0,0,0],[1,0,3,0],[1,0,1,64]].map(c=>Buffer.from(c));
const ALLCAPS=[...CAPS,...BADCAPS];

try{
 // ---------- pins ----------
 assert.equal(filehash('src/source-aware/core.om'),V1_CORE_SHA256,'v1 core bytes changed');
 assert.equal(filehash(FROZEN),FROZEN_SA_SHA256,'frozen v1 codec identity');
 assert.equal(filehash(compiler),COMPILER_SHA256,'compiler identity');
 const vendor=fs.readFileSync('src/source-aware/vendor/sha256.om','utf8'),coreV2=fs.readFileSync('src/source-aware/core-v2.om','utf8'),batchOm=fs.readFileSync('tests/universal/source-aware/batch.om','utf8');
 report.pins={v1CoreSHA256:V1_CORE_SHA256,frozenSA:FROZEN,frozenSASHA256:FROZEN_SA_SHA256,v2CoreSHA256:filehash('src/source-aware/core-v2.om'),v2SASHA256:filehash(NEW),compilerSHA256:COMPILER_SHA256,specSHA256:filehash('spec/universal/source-aware-v2.md'),mainSHA256:filehash('src/source-aware/main.om')};

 // ---------- inputs: #621 corpus (same generation order as gate.mjs), gate edges, evidence, F1 ----------
 const validUnits=[],bad=[];for(let i=0;i<10000;i++)validUnits.push(valid(i));for(let i=0;i<100000;i++)bad.push(damaged(i));
 const s1=unit({source:Buffer.from('a')}),s2=unit({source:Buffer.from(' b\r\n'),spans:[[1,2]]}),b2=unit({ops:[op({imm:1n})]}),missing=unit({missing:true});
 const all=unit({source:Buffer.from(Array.from({length:256},(_,i)=>i))});
 const edges=[s1,s2,b2,missing,all];
 for(const source of [Buffer.alloc(0),Buffer.from([0]),Buffer.alloc(4096,255),Buffer.from('\uFEFF\0\r\n\tμ😀'),Buffer.from([0xf4,0x8f,0xbf,0xbf])])edges.push(unit({source,encoding:source.every(x=>x===255)?0:1}));
 for(const b of validUnits.slice(0,24)){const n=b.readUInt16LE(23),off=13+semLen(b);for(let i=0;i<n;i++)for(const at of [2,3,4,5,6,7,8,10,12]){const d=Buffer.from(b);d[57+i*20+at]^=255;edges.push(d);}const d=Buffer.from(b);d[off+42]^=1;edges.push(d);}
 const evidence=new Map();
 for(const dir of ['evidence/UNIVERSAL-621','evidence/UNIVERSAL-622','evidence/UNIVERSAL-625'])for(const f of fs.readdirSync(dir,{recursive:true})){
  const p=path.join(dir,f);if(!fs.statSync(p).isFile())continue;const bytes=fs.readFileSync(p);
  if(bytes.subarray(0,4).equals(Buffer.from('OMSB')))evidence.set(digest(bytes),{from:p,input:bytes});
  else for(const m of bytes.toString('latin1').matchAll(/4f4d5342(?:[0-9a-f]{2})+/gi)){const e=Buffer.from(m[0],'hex');evidence.set(digest(e),{from:p,input:e});}
 }
 assert.ok(evidence.size>0,'no recorded v1 envelopes found in evidence');
 const f1=JSON.parse(fs.readFileSync('tests/universal/translation/fixtures-f1.json'));const f1Units=[];
 for(const fx of f1.positives){const r=spawnSync('python3',['tools/adapters/c-import'],{input:Buffer.from(fx.source)});
  assert.equal(r.status,0,`F1 import ${fx.id}: ${r.stderr}`);assert.ok(r.stdout.subarray(0,4).equals(Buffer.from('OMSB')),'F1 envelope '+fx.id);f1Units.push({id:fx.id,input:r.stdout});}
 // v1 header sweeps: every semantic version except 2, every language/profile byte, v1 with profiles 7..9.
 const sweepBase=[1,2,3,4,5,6].flatMap(p=>[validUnits[p-1],validUnits[p+5]]),sweeps=[];
 for(const u of sweepBase){for(let v=0;v<256;v++)if(v!==2)sweeps.push(header(u,{version:v}));for(let x=0;x<256;x++){sweeps.push(header(u,{profile:x}));sweeps.push(header(u,{lang:x,dialect:u[18]}));sweeps.push(header(u,{lang:x}));}
  for(const p of [7,8,9])sweeps.push(header(u,{lang:LANG[p],profile:p}));}
 report.inputs={corpusValid:validUnits.length,corpusMalformed:bad.length,gateEdges:edges.length,evidenceEnvelopes:evidence.size,evidenceSources:[...new Set([...evidence.values()].map(e=>e.from))].length,f1Imports:f1Units.length,headerSweeps:sweeps.length};
 console.log('inputs',JSON.stringify(report.inputs));

 // ---------- 1. frozen-v1 equivalence, all six modes, both codecs per process ----------
 const inputs=[...validUnits.map((input,i)=>({set:'corpus-valid',input,allCaps:i<60})),...bad.map(d=>({set:'corpus-malformed:'+d.category,input:d.input,category:d.category})),
  ...edges.map(input=>({set:'gate-edge',input})),...[...evidence.values()].map(e=>({set:'evidence',input:e.input})),...f1Units.map(f=>({set:'F1-import',input:f.input})),...sweeps.map(input=>({set:'header-sweep',input}))];
 const extra=[{set:'empty',input:Buffer.alloc(0)},{set:'oversized',input:Buffer.alloc(8193)},{set:'oversized-legality',input:Buffer.alloc(8197)},{set:'short-caps',input:Buffer.of(1,0,1)}];
 const eq={requests:0,identical:0,excludedByte17Is2:0,bySet:{},byStatus:{}},excluded={};let mismatches=[],k=0;
 const t1=Date.now();
 const items=[...inputs,...extra];
 for(let start=0;start<items.length;start+=8000){
  const reqs=[];items.slice(start,start+8000).forEach((item,j)=>{const idx=start+j;for(const m of [1,2,3,4,5])reqs.push({item,m,input:item.input});
   for(const c of item.allCaps?ALLCAPS:[ALLCAPS[idx%ALLCAPS.length]])reqs.push({item,m:6,input:Buffer.concat([c,item.input])});
   if(idx%50===0)for(const m of [0,7,255])reqs.push({item,m,input:item.input});});
  const res=runAll(reqs.flatMap(q=>[[FROZEN,q.m,q.input],[NEW,q.m,q.input]]));
  reqs.forEach(({item,m,input},j)=>{const a=res[2*j],b=res[2*j+1];eq.requests++;
   const key=item.set.split(':')[0];eq.bySet[key]=(eq.bySet[key]??0)+1;
   if(byte17(m,input)===2){eq.excludedByte17Is2++;assert.ok(a.status!==0&&a.stdout.length===0,'frozen accepted a byte17=2 input');
    const tag=`${item.set} frozen=${a.status} new=${b.status}`;excluded[tag]=(excluded[tag]??0)+1;
    if(item.category==='semantic-version'&&m>=1&&m<=6)assert.equal(b.status,68,'(v2, profile 1) must refuse 68');return;}
   if(same(a,b)){eq.identical++;eq.byStatus[a.status]=(eq.byStatus[a.status]??0)+1;}else if(mismatches.length<20)mismatches.push({set:item.set,mode:m,hex:input.toString('hex').slice(0,400),frozen:a.status,next:b.status});});
  console.log('equivalence',Math.min(start+8000,items.length),'inputs',eq.requests,'requests',Math.round((Date.now()-t1)/1000)+'s');
 }
 eq.byte17Is2Outcomes=excluded;eq.seconds=Math.round((Date.now()-t1)/1000);report.frozenEquivalence=eq;
 assert.equal(mismatches.length,0,'frozen-v1 equivalence: '+JSON.stringify(mismatches));assert.equal(eq.identical+eq.excludedByte17Is2,eq.requests);
 report.steps.push('frozen-v1 equivalence');console.log('1 frozen-v1 equivalence PASS',JSON.stringify({requests:eq.requests,identical:eq.identical,excluded:eq.excludedByte17Is2}));

 // ---------- 3. retargeted corpus (valid v2 units are also the refusal inputs of step 2) ----------
 const p1=validUnits.filter((_,i)=>i%6===0),f1base=f1Units.map(f=>f.input),v2Valid=[];
 for(const u of [...p1,...f1base])for(const p of [7,8,9])v2Valid.push({u,r:retarget(u,p),profile:p});
 const rt={units:v2Valid.length,requests:0,legalityRequests:0,roundtrips:0,malformedPairs:0,malformedStatuses:{},explicit:0};
 {const jobs=[],checks=[];v2Valid.forEach(({u,r},idx)=>{
   for(const m of [1,2,3,4,5]){checks.push({kind:'mode',m,r});jobs.push([NEW,m,r]);}
   for(const c of idx<60?ALLCAPS:[ALLCAPS[idx%ALLCAPS.length]]){checks.push({kind:'legality',u,c});jobs.push([FROZEN,6,Buffer.concat([c,u])],[NEW,6,Buffer.concat([c,r])]);}});
  const res=runAll(jobs);let j=0,decoded=[];
  for(const q of checks){if(q.kind==='mode'){const o=res[j++];rt.requests++;assert.equal(o.status,0,`retarget mode ${q.m}`);assert.ok(o.stdout.equals(expected(q.m,q.r)),`retarget mode ${q.m} bytes`);if(q.m===2)decoded.push({r:q.r,out:o.stdout});}
   else{const v1=res[j++],v2=res[j++];rt.legalityRequests++;assert.ok(same(v1,v2),'retargeted legality differs from v1');if(v1.status===0)assert.ok(v2.stdout.equals(legality(inspect(q.u),[...q.c])),'legality vs reference');}}
  // decode -> encode: the decoded output is fed back to encode and must reproduce the input.
  runAll(decoded.map(d=>[NEW,1,d.out])).forEach((o,i)=>{rt.roundtrips++;assert.ok(o.status===0&&o.stdout.equals(decoded[i].r),'decode->encode');});}
 // Malformed retargets: the same corruption gets the same verdict as on the original v1 profile-1 unit.
 const mutations=[];for(const c of categories)if(c.name!=='semantic-version')mutations.push({name:c.name,f:b=>c.mutate(Buffer.from(b))});
 for(let i=0;i<14;i++)for(const at of [2,3,4,5,6,7,8,10,12])for(const re of [false,true])mutations.push({name:`op${i}+${at}${re?'-rehash':''}`,f:b=>{if(i>=b.readUInt16LE(23))return null;const d=Buffer.from(b);d[57+i*20+at]^=255;return re?rehash(d):d;}});
 const malPairs=[];for(const u of p1.slice(0,60))for(const p of [7,8,9])for(const mu of mutations)malPairs.push({u,p,mu});
 {const pairs=[];for(const {u,p,mu} of malPairs){const a=mu.f(u);if(a)pairs.push({name:mu.name,a,b:mu.f(retarget(u,p))});}
  const res=runAll(pairs.flatMap(q=>[[FROZEN,2,q.a],[NEW,2,q.b]]));
  pairs.forEach((q,i)=>{const x=res[2*i],y=res[2*i+1];rt.malformedPairs++;assert.equal(y.status,x.status,`retargeted ${q.name} verdict`);
   if(x.status===0)assert.ok(y.stdout.equals(q.b),'retargeted mutant roundtrip');else assert.equal(y.stdout.length,0);rt.malformedStatuses[x.status]=(rt.malformedStatuses[x.status]??0)+1;});}
 // Explicit v2 rules with hand-stated expected statuses from the spec.
 const base=p1[0],explicit=[];const ex=(name,input,code)=>explicit.push({name,input,code});
 for(const p of [1,2,3,4,5,6]){const u=validUnits[p-1];assert.equal(u[19],p);ex(`v2 with profile ${p}`,header(u,{version:2}),68);}
 for(const p of [7,8,9])ex(`v1 with profile ${p}`,retarget(base,p,1),68);
 for(const p of [7,8,9]){ex(`v2 profile ${p} valid`,retarget(base,p),0);ex(`v2 profile ${p} version 3`,header(retarget(base,p),{version:3}),66);ex(`v2 profile ${p} version 255`,header(retarget(base,p),{version:255}),66);ex(`v2 profile ${p} version 0`,header(retarget(base,p),{version:0}),66);
  for(const l of [0,1,2,3,4,5,6,7,8,255])if(l!==LANG[p]){ex(`v2 profile ${p} language ${l}`,header(retarget(base,p),{lang:l}),68);ex(`v2 profile ${p} header ${LANG[p]} dialect ${l}`,header(retarget(base,p),{dialect:l}),68);
   const one=Buffer.from(retarget(base,p));one[57+20*1]=l;ex(`v2 profile ${p} second op dialect ${l}`,rehash(one),68);}
  ex(`v2 profile ${p} runtime 1`,header(retarget(base,p),{runtime:1}),68);ex(`v2 profile ${p} ABI 2`,header(retarget(base,p),{abi:2}),68);
  for(const [what,at,val] of [['envelope version',4,2],['dialect version',20,2],['op version',58,2]]){const d=Buffer.from(retarget(base,p));d[at]=val;ex(`v2 profile ${p} ${what}`,at>=13?rehash(d):d,66);}
  {const d=Buffer.from(retarget(base,p));d[13+semLen(d)+4]=2;ex(`v2 profile ${p} sidecar version`,d,66);}
  for(const q of [0,10,255])ex(`v2 profile ${q} under language ${LANG[p]}`,header(retarget(base,p),{profile:q}),68);
  const L=LANG[p],mk=ops=>header(unit({ops:ops.map(o=>op({lang:1,...o}))}),{version:2,lang:L,profile:p});
  const inA={code:9,attr:0,ob:1,imm:0n},inB={code:9,attr:0,ob:1,imm:1n};
  ex(`v2 profile ${p} div obligations 3`,mk([inA,inB,{code:5,ob:3,a:0,b:1}]),0);
  ex(`v2 profile ${p} div obligations 1`,mk([inA,inB,{code:5,ob:1,a:0,b:1}]),72);
  ex(`v2 profile ${p} add obligations 5 (masking)`,mk([inA,inB,{code:2,ob:5,a:0,b:1}]),72);
  ex(`v2 profile ${p} add obligations 0`,mk([inA,inB,{code:2,ob:0,a:0,b:1}]),72);
  ex(`v2 profile ${p} mul obligations 3`,mk([inA,inB,{code:4,ob:3,a:0,b:1}]),72);
  ex(`v2 profile ${p} constant obligations 1`,mk([inA,{code:1,ob:1}]),72);
  ex(`v2 profile ${p} input obligations 0`,mk([{...inA,ob:0}]),72);
  ex(`v2 profile ${p} input obligations 5`,mk([{...inA,ob:5}]),72);
  ex(`v2 profile ${p} add effects`,mk([inA,inB,{code:2,ob:1,a:0,b:1,effect:1}]),71);
  ex(`v2 profile ${p} add alias`,mk([inA,inB,{code:2,ob:1,a:0,b:1,alias:1}]),71);
  ex(`v2 profile ${p} bigint constant`,mk([{code:1,type:2,attr:2}]),68);
  ex(`v2 profile ${p} dynamic call`,mk([{code:9,type:3,attr:0,ob:1},{code:6,type:3,attr:3,effect:63,alias:255,ob:8,a:0}]),68);
  ex(`v2 profile ${p} byte read`,mk([{code:9,type:4,attr:0,alias:1,ob:1},inB,{code:7,attr:4,effect:17,alias:1,ob:24,a:0,b:1}]),68);
  ex(`v2 profile ${p} opcode 10`,mk([inA,{code:10}]),69);
  ex(`v2 profile ${p} forward operand`,mk([inA,inB,{code:2,ob:1,a:3,b:1},{code:2,ob:1,a:0,b:1}]),70);
  ex(`v2 profile ${p} duplicate slot`,mk([inA,{...inB,imm:0n}]),70);
  ex(`v2 profile ${p} slot 6`,mk([{...inA,imm:6n}]),67);
  ex(`v2 profile ${p} op 6 on u64`,mk([inA,{code:6,attr:3,effect:63,alias:255,ob:8,a:0}]),68);
  ex(`v2 profile ${p} op 7 on u64`,mk([inA,inB,{code:7,attr:4,effect:17,alias:1,ob:24,a:0,b:1}]),68);
  ex(`v2 profile ${p} op 8 on u64`,mk([inA,inB,{code:8,attr:4,effect:19,alias:1,ob:24,a:0,b:1,imm:1n}]),68);
 }
 const explicitRes=runAll(explicit.flatMap(e=>[[NEW,2,e.input],[FROZEN,2,e.input]]));
 explicit.forEach((e,i)=>{const o=explicitRes[2*i],f=explicitRes[2*i+1];rt.explicit++;e.out=o;
  assert.equal(o.status,e.code,`explicit '${e.name}'`);assert.ok(e.code?o.stdout.length===0:o.stdout.equals(e.input),`explicit '${e.name}' output`);
  if(e.input[17]!==2)assert.ok(same(o,f),`explicit '${e.name}' frozen equivalence`);
  const pub=spawnSync('sh',['tools/source-aware/sa','decode'],{input:e.input,env:{...process.env,SA_BUILD:root}});assert.equal(pub.status,e.code,`public '${e.name}'`);assert.equal(pub.stdout.length,e.code?0:e.input.length);});
 const explicitValid=explicit.filter(e=>e.code===0&&e.input[17]===2).map(e=>e.input);
 report.retargeted={...rt,profiles:[7,8,9],baseUnits:{corpusProfile1:p1.length,f1Imports:f1base.length},mutationsPerUnit:mutations.length,explicitCases:explicit.map(e=>({name:e.name,code:e.code}))};
 report.steps.push('retargeted corpus');console.log('3 retargeted corpus PASS',JSON.stringify(rt));

 // ---------- 2. frozen codec and v1 reference refuse every valid v2 unit (66, empty stdout) ----------
 const refusal={units:0,frozenRequests:0,referenceChecks:0,publicChecks:0,consumers:{}};
 const allV2=[...v2Valid.map(v=>v.r),...explicitValid];
 for(const r of allV2){refusal.units++;refusal.referenceChecks++;assert.equal(inspect(r).code,66,'reference.mjs must refuse v2 with 66');}
 runAll(allV2.flatMap((r,idx)=>[...[1,2,3,4,5].map(m=>[FROZEN,m,r]),[FROZEN,6,Buffer.concat([CAPS[idx%CAPS.length],r])]])).forEach(o=>{refusal.frozenRequests++;assert.equal(o.status,66,'frozen v1 must refuse v2 with 66');assert.equal(o.stdout.length,0);});
 for(const r of allV2.slice(0,12))for(const c of ['decode','identity','replay']){const o=spawnSync('sh',['tools/source-aware/sa',c],{input:r});refusal.publicChecks++;assert.equal(o.status,66);assert.equal(o.stdout.length,0);assert.ok(o.stderr.toString().includes('UNKNOWN_VERSION'));}
 // Consumers built on the v1 core (tools/adapters/lower -> build/L831, tools/adapters/c-export -> build/L622).
 for(const tool of ['tools/adapters/lower','tools/adapters/c-export']){const row={units:0,statuses:{}};
  for(const r of allV2.filter((_,i)=>i%25===0)){const o=spawnSync('python3',[tool],{input:r});row.units++;row.statuses[o.status]=(row.statuses[o.status]??0)+1;
   assert.ok(o.status!==0&&o.status!==78,`${tool} must refuse v2 natively, got ${o.status}: ${o.stderr}`);assert.equal(o.stdout.length,0,`${tool} stdout`);}
  refusal.consumers[tool]=row;}
 report.frozenRefusal=refusal;report.steps.push('frozen refusal');console.log('2 frozen refusal PASS',JSON.stringify(refusal));

 // ---------- 4. stale state: interleaved v1/v2 success/failure ----------
 const mixed=[];const add=(mode,input)=>mixed.push({mode,input});
 for(let i=0;i<120;i++){const u=validUnits[i],r=v2Valid[i].r;for(const m of [1,2,3,4,5]){add(m,u);add(m,r);}add(6,Buffer.concat([CAPS[i%CAPS.length],u]));add(6,Buffer.concat([CAPS[(i+7)%CAPS.length],r]));
  add(2,bad[i].input);add(1+i%5,explicit[i%explicit.length].input);add(6,Buffer.concat([BADCAPS[i%BADCAPS.length],r]));add(i%2?0:7,r);add(2,header(r,{version:i%2?1:3}));}
 add(6,Buffer.of(1,0,1));add(2,Buffer.alloc(0));
 const isolated=runAll(mixed.map(q=>[NEW,q.mode,q.input]));
 const prod=compile(vendor+coreV2+batchOm,'batch-v2');
 let state=0x8327;const rnd=()=>{state^=state<<13;state^=state>>>17;state^=state<<5;return state>>>0;};
 const shuffled=mixed.map((_,i)=>i);for(let i=shuffled.length-1;i>0;i--){const j=rnd()%(i+1);[shuffled[i],shuffled[j]]=[shuffled[j],shuffled[i]];}
 // 64 operations and a 4096-byte source: the largest outputs any request can leave behind.
 const bigOps=[op({code:9,attr:0,ob:1,imm:0n}),op({code:9,attr:0,ob:1,imm:1n})];while(bigOps.length<64)bigOps.push(op({code:2+bigOps.length%3,ob:1,a:bigOps.length-1,b:bigOps.length-2}));
 const big=header(unit({ops:bigOps,source:Buffer.alloc(4096,0xab)}),{version:2,lang:6,profile:8}),fail=header(p1[1],{version:2});
 assert.equal(runAll([[NEW,1,big]])[0].status,0,'big v2 unit');
 const orders={forward:mixed.map((_,i)=>i),reverse:mixed.map((_,i)=>mixed.length-1-i),shuffled,
  sandwiched:mixed.flatMap((_,i)=>[-1,i,-2,i,-3,i])};
 const stale={requests:mixed.length,orders:{}};
 const pick=i=>i===-1?{mode:1,input:big}:i===-2?{mode:2,input:fail}:i===-3?{mode:6,input:Buffer.concat([CAPS[11],big])}:mixed[i];
 for(const [name,order] of Object.entries(orders)){const res=batch(prod.exe,order.map(pick));assert.equal(res.status,0,'batch status '+name);assert.ok(res.complete,'batch complete '+name);
  let checked=0;order.forEach((i,j)=>{if(i<0)return;const want=isolated[i],got=res.rows[j];const wantStatus=want.status;
   assert.equal(got.status,wantStatus,`stale ${name} #${j} status`);assert.ok(got.stdout.equals(want.stdout),`stale ${name} #${j} bytes`);checked++;});
  stale.orders[name]={requests:order.length,checkedAgainstIsolated:checked};}
 // Public one-request-per-process driver in an interleaved order.
 let pubChecked=0;const names={1:'encode',2:'decode',3:'identity',4:'provenance',5:'replay',6:'legality'};
 for(const i of shuffled.slice(0,240)){const q=mixed[i];if(!names[q.mode])continue;const o=spawnSync('sh',['tools/source-aware/sa',names[q.mode]],{input:q.input,env:{...process.env,SA_BUILD:root}});
  assert.equal(o.status,isolated[i].status,'public stale status');assert.ok(o.stdout.equals(isolated[i].stdout),'public stale bytes');pubChecked++;}
 stale.publicDriverChecked=pubChecked;
 // Direct core caller: v2 success, then v1 success, then failures; version byte of each encode and El reset.
 const load=b=>[...b.entries()].map(([i,c])=>`put Ib ${i} ${c}\n`).join('');const v2s=retarget(s1,9),v1s=s1;
 let caller='fn main to int\nconstants\n'+load(v2s)+`code = perform 1 ${v2s.length}\nif ne code 0\nret 1\nend\nif ne get El 0 ${v2s.length}\nret 2\nend\nif ne get Eb 17 2\nret 3\nend\n`;
 caller+=load(v1s)+`code = perform 2 ${v1s.length}\nif ne code 0\nret 4\nend\nif ne get Eb 17 1\nret 5\nend\nif ne get Eb 19 1\nret 6\nend\n`;
 caller+=`code = decode 0\nif ne code 65\nret 7\nend\nif ne get El 0 0\nret 8\nend\n`;
 const v2bad=header(v2s,{version:3});caller+=load(v2bad)+`put El 0 12\ncode = perform 1 ${v2bad.length}\nif ne code 66\nret 9\nend\nif ne get El 0 0\nret 10\nend\n`;
 const v2low=header(s1,{version:2});caller+=load(v2low)+`put El 0 12\ncode = perform 2 ${v2low.length}\nif ne code 68\nret 11\nend\nif ne get El 0 0\nret 12\nend\n`;
 caller+=load(v1s)+`code = perform 1 ${v1s.length}\nif ne code 0\nret 13\nend\nif ne get Eb 17 1\nret 14\nend\nret 0\nend\n`;
 const direct=compile(vendor+coreV2+caller,'direct-v2');const dr=spawnSync(direct.exe);assert.equal(dr.status,0,'direct core caller step '+dr.status);assert.equal(dr.stdout.length,0);
 stale.directCore={...direct,status:dr.status};report.staleState=stale;report.batch=prod;report.steps.push('stale state');console.log('4 stale state PASS',JSON.stringify({requests:stale.requests,orders:stale.orders,publicDriverChecked:pubChecked}));

 // ---------- compiled v2 mutants must be caught by the controls above ----------
 const controls=[...orders.forward.map(pick),...explicit.map(e=>({mode:2,input:e.input}))];
 const want=[...isolated,...explicit.map(e=>e.out)];
 const mutants=[
  ['encode-version-hardcoded',s=>s.replace('ebyte get M 8\nebyte get M 0','ebyte 1\nebyte get M 0')],
  ['v2-low-profile-admitted',s=>s.replace('if ne get M 8 1\nret 68','if false\nret 68')],
  ['v1-high-profile-admitted',s=>s.replace('if ne get M 8 2\nret 68','if false\nret 68')],
  ['version-3-admitted',s=>s.replace('if and ne get M 8 1 ne get M 8 2','if false')],
  ['zig-language-unchecked',s=>s.replace('if eq p 8\nif ne l 6','if eq p 8\nif false')],
  ['profile-10-admitted',s=>s.replace('if gt p 9\nret 68','if gt p 10\nret 68')],
 ];
 report.mutants=[];
 for(const [name,mutate] of mutants){const changed=mutate(coreV2);assert.notEqual(changed,coreV2,'mutant did not apply '+name);const built=compile(vendor+changed+batchOm,'mutant-'+name);
  const res=batch(built.exe,controls);let wrong=res.complete?0:1;res.rows.forEach((g,i)=>{if(!same(g,want[i]))wrong++;});assert.ok(wrong>0,'survived '+name);report.mutants.push({name,...built,executed:res.rows.length,mismatches:wrong,caught:true});}
 report.steps.push('mutants');console.log('mutants caught',report.mutants.map(m=>`${m.name}:${m.mismatches}`).join(' '));

 report.processRequests=spawned;report.verdict='PASS';fs.writeFileSync(path.join(run,'receipt.json'),JSON.stringify(report,null,2)+'\n');fs.writeFileSync(path.join(root,'PASS.json'),JSON.stringify(report,null,2)+'\n');
 console.log(JSON.stringify({verdict:'PASS',equivalence:{requests:eq.requests,identical:eq.identical,excludedByte17Is2:eq.excludedByte17Is2},retargeted:{units:rt.units,modeRequests:rt.requests,legalityPairs:rt.legalityRequests,decodeEncode:rt.roundtrips,malformedPairs:rt.malformedPairs,explicit:rt.explicit},v2RefusedByFrozen:{units:refusal.units,requests:refusal.frozenRequests},staleBatchRequests:Object.values(stale.orders).reduce((a,o)=>a+o.requests,0),mutantsCaught:report.mutants.length,singleProcessRequests:spawned,run}));
}catch(e){report.verdict='FAIL';report.failure={message:e.message,stack:e.stack};fs.writeFileSync(path.join(run,'failure.json'),JSON.stringify(report,null,2)+'\n');fs.rmSync(path.join(root,'PASS.json'),{force:true});console.error(e.stack);process.exitCode=1;}
