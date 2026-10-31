// Independent development gate. Node is orchestration/reference only, never production.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync,execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {hash,unit,op,u32,expected,parent} from './fixtures.mjs';
import {inspect,legality} from './reference.mjs';
import {seed,valid,damaged,categories} from './corpus.mjs';
const root='build/L574',run=path.join(root,'runs',new Date().toISOString().replaceAll(':','-'));
fs.mkdirSync(run,{recursive:true});fs.rmSync(path.join(root,'PASS.json'),{force:true});
const report={schema:'omega.source-aware.development.v1',fidelity:'DIFFERENTIAL_COMPATIBILITY',host:'HOST_NATIVE',seed,run,sourceHEAD:execFileSync('git',['rev-parse','HEAD']).toString().trim(),base625:'69790fb4ad1e803ca79245430745b96129d1dcf4',review:'PENDING',signed:false,notRun:['full local judge','independent API/core review','translation proof','general language adapters','GPU/hardware targets'],steps:[]};
let compiler=path.join(root,'toolchain/build/bootstrap/omc'),core=fs.readFileSync('src/source-aware/core.om','utf8'),vendor=fs.readFileSync('src/source-aware/vendor/sha256.om','utf8'),batch=fs.readFileSync('tests/universal/source-aware/batch.om','utf8');
const digest=b=>hash(b).toString('hex');
const filehash=p=>digest(fs.readFileSync(p));
function command(exe,args=[],input,options={}){const r=spawnSync(exe,args,{input,maxBuffer:512*1024*1024,timeout:180000,...options});if(r.error&&!(options.allowEarlyExit&&r.error.code==='EPIPE'))throw r.error;return r;}
function compile(source,name){const src=path.join(run,name+'.om'),exe=path.join(run,name);fs.writeFileSync(src,source);let r=command(compiler,[],Buffer.from(source));fs.writeFileSync(src+'.stderr',r.stderr);assert.equal(r.status,0,`${name} compilation: ${r.stderr}`);assert.ok(r.stdout.length>0);fs.writeFileSync(exe,r.stdout,{mode:0o700});let elf=command('sh',[path.join(root,'toolchain/tools/verify-elf.sh'),exe]);assert.equal(elf.status,0,'AArch64 ELF required');return {exe,sourceSHA256:digest(source),binarySHA256:digest(r.stdout),compileStatus:r.status};}
function runBatch(exe,requests,{strict=true}={}){
 let input=Buffer.concat(requests.map(r=>Buffer.concat([Buffer.of(r.mode),u32(r.input.length),r.input])));const result=command(exe,[],input,{allowEarlyExit:!strict});let out=result.stdout,pos=0,seen=0,wrong=[],executedCategories={};
 for(let [i,r] of requests.entries()){
  if(pos+37>out.length){wrong.push({i,why:'missing response',remaining:out.length-pos});break;}
  let code=out[pos],len=out.readUInt32LE(pos+1),consumed=out.subarray(pos+5,pos+37),d=hash(Buffer.concat([Buffer.of(r.mode),r.input]));pos+=37;
  assert.ok(consumed.equals(d),`consumed input mismatch ${i}`);assert.ok(pos+len<=out.length,'truncated response');let bytes=out.subarray(pos,pos+len);pos+=len;seen++;let category=r.category??'other';executedCategories[category]=(executedCategories[category]??0)+1;
  if(code!==r.code||!bytes.equals(r.want))wrong.push({i,category:r.category,expectedCode:r.code,code,expectedBytes:r.want.length,bytes:len});
 }
 if(result.status!==0||pos!==out.length||seen!==requests.length)wrong.push({status:result.status,signal:result.signal,seen,requested:requests.length,pos,outLength:out.length,stderr:result.stderr.toString()});
 const row={requested:requests.length,executed:seen,executedCategories,mismatches:wrong.length,requestStreamSHA256:digest(input),responseStreamSHA256:digest(out),status:result.status,sampleFailures:wrong.slice(0,8)};
 if(strict)assert.equal(wrong.length,0,JSON.stringify(row));return row;
}
const request=(mode,input,code=0,want)=>({mode,input,code,want:want??(code?Buffer.alloc(0):expected(mode,input))});
function malformedRequest(input){const ref=inspect(input);assert.notEqual(ref.code,0);return request(2,input,ref.code);}
try{
 report.toolchain={compilerSHA256:filehash(compiler),compilerSourceSHA256:filehash(path.join(root,'toolchain/seed/omc/omc.om')),bridgeCommit:'0fc7d8662cb799e7c206b17c18ea4c3ec2b8fea9',seedCommit:'29b12760f4c962606411d6bf25032aedef063301',imports:filehash('tools/source-aware/imports.sha256'),nativeSourceSHA256:digest(vendor+core+fs.readFileSync('src/source-aware/main.om')),nativeBinarySHA256:filehash(path.join(root,'sa')),oracle:'Node '+process.version+' Buffer, TextDecoder and OpenSSL SHA256'};
 assert.equal(report.toolchain.compilerSHA256,'191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9');
 const prod=compile(vendor+core+batch,'batch');report.batch=prod;
 let validUnits=[],bad=[],requests=[],counts={},samples={},validDigest=createHash('sha256'),badDigest=createHash('sha256');const uniqueV=new Set(),uniqueB=new Set();
 for(let i=0;i<10000;i++){
  let b=valid(i),ref=inspect(b);assert.equal(ref.code,0,`valid membership ${i}: ${ref.why}`);let h=digest(b);assert.ok(!uniqueV.has(h),'valid corpus duplicate');uniqueV.add(h);validDigest.update(b);validUnits.push(b);requests.push({...request(i%2+1,b),category:'valid roundtrip'});
  requests.push({...request(3,b),category:'valid semantic identity'});requests.push({...request(5,b),category:'valid replay'});
  if(i%10===0)requests.push({...request(4,b),category:'valid provenance identity'});
 }
 for(let i=0;i<100000;i++){
  let d=damaged(i),ref=inspect(d.input);assert.equal(ref.code,d.code,`malformed membership ${i} ${d.category}: ${ref.why}`);assert.notEqual(ref.code,0);let h=digest(d.input);assert.ok(!uniqueB.has(h),'malformed duplicate '+d.category);uniqueB.add(h);badDigest.update(d.input);
  counts[d.category]=(counts[d.category]??0)+1;if(!samples[d.category])samples[d.category]={expectedCode:d.code,hex:d.input.toString('hex'),SHA256:h};bad.push(d);requests.push({...request(i%2+1,d.input,d.code),category:d.category});
 }
 assert.equal(Object.keys(counts).length,categories.length);assert.equal(Object.values(counts).reduce((a,b)=>a+b),100000);
 fs.writeFileSync(path.join(run,'counterexamples.json'),JSON.stringify(samples,null,2)+'\n');
 console.log('Generated and independently classified 10000 unique valid and 100000 unique malformed units. Running native core.');
 let structural=runBatch(prod.exe,requests);for(let [category,n] of Object.entries(counts))assert.equal(structural.executedCategories[category],n);report.structural={...structural,validUnits:validUnits.length,uniqueValid:uniqueV.size,malformedUnits:bad.length,uniqueMalformed:uniqueB.size,categoryCounts:counts,validCorpusSHA256:validDigest.digest('hex'),malformedCorpusSHA256:badDigest.digest('hex'),perRequestConsumedSHA256Checked:true,validRoundtripExecuted:structural.executedCategories['valid roundtrip'],semanticDigestsIndependentlyChecked:structural.executedCategories['valid semantic identity'],exactReplayExecuted:structural.executedCategories['valid replay'],provenanceDigestsIndependentlyChecked:structural.executedCategories['valid provenance identity']};
 report.steps.push('structural');fs.writeFileSync(path.join(run,'progress.json'),JSON.stringify(report,null,2));
 // Every typed operation/profile and capability combination, including retained operations.
 let legalRequests=[];
 for(let b of validUnits.slice(0,60))for(let target of [1,2])for(let runtime of [0,1,2])for(let abi of [1,2])for(let effects of [0,17,63]){
  const caps=Buffer.from([target,runtime,abi,effects]),ref=inspect(b);legalRequests.push(request(6,Buffer.concat([caps,b]),0,legality(ref,caps)));
 }
 for(let caps of [[0,0,1,0],[3,0,1,0],[1,3,1,0],[1,0,0,0],[1,0,3,0],[1,0,1,64]])legalRequests.push(request(6,Buffer.concat([Buffer.from(caps),unit()]),68));
 report.legality=runBatch(prod.exe,legalRequests);
 // Identity field separation and profile/behavior variation checked against independent SHA256.
 const s1=unit({source:Buffer.from('a')}),s2=unit({source:Buffer.from(' b\r\n'),spans:[[1,2]]});
 assert.ok(expected(3,s1).equals(expected(3,s2)));assert.ok(!expected(4,s1).equals(expected(4,s2)));
 const b2=unit({ops:[op({imm:1n})]});assert.ok(!expected(3,s1).equals(expected(3,b2)));
 const missing=unit({missing:true});let edge=[request(3,s1),request(3,s2),request(4,s1),request(4,s2),request(3,b2),request(1,missing),request(5,missing,73)];
 // UTF8 controls and source encodings at their finite bounds, plus every source byte.
 const all=unit({source:Buffer.from(Array.from({length:256},(_,i)=>i))});edge.push(request(5,all));
 for(let source of [Buffer.alloc(0),Buffer.from([0]),Buffer.alloc(4096,255),Buffer.from('\uFEFF\0\r\n\tμ😀'),Buffer.from([0xf4,0x8f,0xbf,0xbf])]){
  let b=unit({source,encoding:source.every(x=>x===255)?0:1});assert.equal(inspect(b).code,0);for(let mode of [1,2,3,4,5])edge.push(request(mode,b));
 }
 // Structural deep negatives on actually typed bigint/dynamic/memory inputs.
 for(let b of validUnits.slice(0,24)){
  let ref=inspect(b),off=13+b.readUInt32LE(5);for(let i=0;i<ref.ops.length;i++)for(let at of [2,3,4,5,6,7,8,10,12]){
   let d=Buffer.from(b);d[57+i*20+at]^=255;let r=inspect(d);if(r.code!==0)edge.push(request(2,d,r.code));else edge.push(request(2,d));
  }
  let d=Buffer.from(b);d[off+42]^=1;edge.push(request(5,d,74));
 }
 report.edges=runBatch(prod.exe,edge);
 // Real public commands, direct native entry and named failures. No redirection artifact claims.
 let publicRows=[];
 for(let [name,mode] of [['encode',1],['decode',2],['identity',3],['provenance',4],['replay',5]]){
  let r=command('sh',['tools/source-aware/sa',name],all);assert.equal(r.status,0);assert.ok(r.stdout.equals(expected(mode,all)));publicRows.push({name,status:r.status,bytes:r.stdout.length});
 }
 for(let sample of Object.values(samples))for(let name of ['decode','replay']){
  let r=command('sh',['tools/source-aware/sa',name],Buffer.from(sample.hex,'hex'));assert.equal(r.status,sample.expectedCode);assert.equal(r.stdout.length,0);assert.ok(r.stderr.length>0);publicRows.push({name,expectedCode:sample.expectedCode,status:r.status,bytes:0});
 }
 for(let [input,code,label] of [[missing,73,'PROVENANCE_MISSING'],[Buffer.alloc(8193),67,'LIMIT']]){
  let r=command('sh',['tools/source-aware/sa','replay'],input);assert.equal(r.status,code);assert.equal(r.stdout.length,0);assert.ok(r.stderr.toString().includes(label));publicRows.push({name:label,status:r.status,bytes:0});
 }
 for(let args of [[],['wat'],['decode','extra']]){let r=command('sh',['tools/source-aware/sa',...args],all);assert.equal(r.status,64);assert.equal(r.stdout.length,0);publicRows.push({name:'BAD_COMMAND',status:r.status});}
 const caps=Buffer.of(2,0,1,63),r=command('sh',['tools/source-aware/sa','legality'],Buffer.concat([caps,s1]));assert.equal(r.status,0);assert.ok(r.stdout.equals(legality(inspect(s1),caps)));publicRows.push({name:'legality',status:r.status});
 // Direct core caller succeeds then fails, across direct decode/perform entry points.
 let caller='fn main to int\nconstants\n';for(let [i,c] of s1.entries())caller+=`put Ib ${i} ${c}\n`;
 caller+=`code = perform 1 ${s1.length}\nif ne code 0\nret 1\nend\nif eq get El 0 0\nret 2\nend\ncode = decode 0\nif ne code 65\nret 3\nend\nif ne get El 0 0\nret 4\nend\nput El 0 12\ncode = perform 6 8193\nif ne code 67\nret 5\nend\nif ne get El 0 0\nret 6\nend\nput El 0 12\ncode = perform 0 0\nif ne code 64\nret 7\nend\nif ne get El 0 0\nret 8\nend\nret 0\nend\n`;
 let direct=compile(vendor+core+caller,'direct');let dr=command(direct.exe);assert.equal(dr.status,0);assert.equal(dr.stdout.length,0);report.directCore=direct;report.publicCommands={requested:publicRows.length,executed:publicRows.length,rows:publicRows};
 // Actual build failure after a prior success marker, in an isolated copy of owned sources.
 const scratch=path.join(run,'build-failure-probe');fs.mkdirSync(scratch,{recursive:true});
 for(let p of ['tools/source-aware','src/source-aware','spec/universal'])fs.cpSync(p,path.join(scratch,p),{recursive:true});
 const sb=path.join(scratch,'build/L574');fs.mkdirSync(sb,{recursive:true});
 fs.cpSync(path.join(root,'toolchain'),path.join(sb,'toolchain'),{recursive:true});
 fs.writeFileSync(path.join(sb,'PASS.json'),'stale success');fs.writeFileSync(path.join(sb,'sa'),'stale executable');
 fs.appendFileSync(path.join(scratch,'src/source-aware/core.om'),'\nINVALID_OMEGA_SYNTAX\n');
 let failedBuild=command('bash',[path.join(scratch,'tools/source-aware/build')]);fs.writeFileSync(path.join(run,'expected-build-failure.log'),failedBuild.stderr);
 assert.notEqual(failedBuild.status,0);for(let p of ['PASS.json','sa','sa.tmp'])assert.ok(!fs.existsSync(path.join(sb,p)),'stale artifact '+p);
 report.failureLifecycle={actualBuildFailureStatus:failedBuild.status,partialExecutableAbsent:true,staleExecutableAbsent:true,stalePASSAbsent:true};
 report.steps.push('legality/identity/edges/public/direct');console.log('Structural, capabilities, public commands and direct transactional checks passed. Compiling semantic mutants.');
 // Small decisive corpus for each independently compiled semantic mutant.
 let controls=[...requests.slice(0,310),...edge,...bad.slice(0,categories.length).map(d=>request(2,d.input,d.code)),...legalRequests];
 let mutants=[
  ['profile-identity',s=>s.replace('part 13 get M 5\ndigout','put Ib 19 1\npart 13 get M 5\ndigout')],
  ['bigint-wrapped-legality',s=>s.replace('if or eq get M 1 3 eq get M 1 6\nret 4','if or eq get M 1 3 eq get M 1 6\nret 3')],
  ['source-hash-bypass',s=>s.replace('if eq hashed start n add off 42 false','if false')],
  ['span-bound-bypass',s=>s.replace('if or gt a b gt b n','if false')],
  ['constant-effects-bypass',s=>s.replace('ret pureop i','ret 0')],
  ['output-reset-missing',s=>s.replaceAll('put El 0 0','note mutant output reset omitted')],
  ['dropped-obligation-encoding',s=>s.replace('ebyte get Oob i','ebyte 0')],
  ['unknown-version-admitted',s=>s.replace('if ne get Ib 17 1','if false')],
  ['UTF8-overlong-admitted',s=>s.replace('if lt v low','if false')],
 ];
 report.mutants=[];
 for(let [name,mutate] of mutants){let changed=mutate(core);assert.notEqual(changed,core);let built=compile(vendor+changed+batch,'mutant-'+name);let result=runBatch(built.exe,controls,{strict:false});assert.ok(result.mismatches>0,'survived '+name);assert.ok(result.executed>0,'never executed '+name);report.mutants.push({name,...built,...result,caught:true});}
 report.steps.push('mutants');
 // Lower API compatibility is a separate test-only legacy host lane.
 const legacyScript=path.resolve('tests/universal/source-aware/legacy.mjs');let lr=command(process.execPath,[legacyScript,run]);fs.writeFileSync(path.join(run,'legacy.log'),Buffer.concat([lr.stdout,lr.stderr]));assert.equal(lr.status,0,'legacy lane '+lr.stderr);report.legacy=JSON.parse(fs.readFileSync(path.join(run,'legacy.json')));
 report.steps.push('lower compatibility');
 report.fixtureFiles={};for(let p of ['spec/universal/source-aware-v1.md','src/source-aware/core.om','src/source-aware/main.om','tests/universal/source-aware/fixtures.mjs','tests/universal/source-aware/reference.mjs','tests/universal/source-aware/corpus.mjs','tests/universal/source-aware/batch.om','tests/universal/source-aware/gate.mjs','tests/universal/source-aware/legacy.c','tests/universal/source-aware/legacy.mjs'])report.fixtureFiles[p]=filehash(p);
 report.verdict='PASS';fs.writeFileSync(path.join(run,'receipt.json'),JSON.stringify(report,null,2)+'\n');fs.writeFileSync(path.join(root,'PASS.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({verdict:report.verdict,valid:report.structural.validUnits,malformed:report.structural.malformedUnits,requests:report.structural.executed,mutants:report.mutants.length,run}));
}catch(e){report.verdict='FAIL';report.failure={message:e.message,stack:e.stack};fs.writeFileSync(path.join(run,'failure.json'),JSON.stringify(report,null,2)+'\n');fs.rmSync(path.join(root,'PASS.json'),{force:true});console.error(e.stack);process.exitCode=1;}
