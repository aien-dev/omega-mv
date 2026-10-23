// Development qualification only. Node orchestrates and independently calculates
// observations; all candidate workloads and mutations execute seeded native .om.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync, execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import os from 'node:os';

const digest = b => createHash('sha256').update(b).digest('hex');
const run = fs.mkdtempSync('build/L729/run-');
const files = ['src/source-aware/recursion-v2.om', 'tests/universal/recursion/workloads.om', 'tests/universal/recursion/batch.om'];
const source = files.map(p => fs.readFileSync(p, 'utf8')).join('');
const compiler = 'build/L574/toolchain/build/bootstrap/omc';
const report = {schema:'omega.recursion.development.v2', run, status:'FAIL', signed:false,
  fidelity:'BOUNDED_DIFFERENTIAL_TESTING', sourceHEAD:execFileSync('git',['rev-parse','HEAD']).toString().trim(),
  host:{platform:os.platform(),arch:os.arch(),release:os.release(),cpu:os.cpus()[0].model},
  node:process.version, compilerSHA256:digest(fs.readFileSync(compiler)),
  sourceHashes:Object.fromEntries([...files, 'spec/universal/bounded-recursion-v2.md', 'tests/universal/recursion/gate.mjs', 'tests/universal/recursion/tutorial.om'].map(p=>[p,digest(fs.readFileSync(p))])),
  notRun:['general recursive interchange codec/lowering','source/native translation proof','native cache seam','native verifier worklist','signed Spark judge','independent final-source review','GPU/non-AArch64 execution'],commands:[],mutants:[],measurements:[]};
assert.equal(report.compilerSHA256,'191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9');
let seq = 0;
function command(exe,args=[],input){
  const t=process.hrtime.bigint();
  const r=spawnSync(exe,args,{input,timeout:120000,maxBuffer:64*1024*1024});
  const elapsedMs=Number(process.hrtime.bigint()-t)/1e6;
  const tag=String(seq++).padStart(3,'0');
  fs.writeFileSync(path.join(run,tag+'.stdout'),r.stdout??Buffer.alloc(0));
  fs.writeFileSync(path.join(run,tag+'.stderr'),r.stderr??Buffer.alloc(0));
  const row={exe,args,status:r.status,signal:r.signal,error:r.error?.message,elapsedMs,tag,
    stdinSHA256:input===undefined?null:digest(input),stdoutSHA256:digest(r.stdout??Buffer.alloc(0))};
  report.commands.push(row);return {...r,elapsedMs,row};
}
function compile(text,name){
  const p=path.join(run,name);fs.writeFileSync(p+'.om',text);
  const r=command(compiler,[],Buffer.from(text));assert.equal(r.status,0,`compile ${name}: ${r.stderr}`);
  fs.writeFileSync(p,r.stdout,{mode:0o700});
  assert.equal(command('sh',['build/L574/toolchain/tools/verify-elf.sh',p]).status,0);
  return {path:p,sourceSHA256:digest(text),binarySHA256:digest(r.stdout),compileMs:r.elapsedMs};
}
const U=(1n<<64n)-1n;
function reference(q){
  let status=0,steps=0,depth=0,peak=0,value=0n;
  const enter=()=>{if(status)return false;if(steps===q.fuel)status=80;
    else if(depth===q.frames)status=81;else if((depth+1)*32>q.bytes)status=82;
    if(status)return false;steps++;depth++;peak=Math.max(peak,depth);return true;};
  if(q.fuel>4096||q.frames>64||q.bytes>2048)status=67;
  else if(q.version!==2)status=66;
  else if(q.mode>6||q.nodes.length>64)status=65;
  else if(q.mode===2||q.mode===3){
    if(q.acc!==0n||(q.n!==64n&&q.n>=BigInt(q.nodes.length)))status=65;
  }else if(q.nodes.length)status=65;
  for(const [i,t] of q.nodes.entries())if(t[1]!==64&&t[1]>=i||t[2]!==64&&t[2]>=i)status=65;
  if(!status){
    if(q.mode<2){
      // Independent closed-form value; separate finite resource trace.
      const calls=q.n+1n;
      while(BigInt(steps)<calls&&enter()){}
      value=(q.acc+q.n*(q.n+1n)/2n)&U;depth=0;
    }else if(q.mode<4){
      // Explicit event stack, distinct from native recursive and phased baselines.
      const events=[['enter',Number(q.n)]];let sum=0n;
      while(events.length&&!status){const [kind,id]=events.pop();
        if(kind==='leave'){depth--;continue;}
        if(!enter())break;
        events.push(['leave',id]);
        if(id!==64){const [v,l,r]=q.nodes[id];sum=(sum+v)&U;events.push(['enter',r],['enter',l]);}
      }value=sum;depth=0;
    }else if(q.mode===4){while(enter()){}depth=0;}
    else if(q.mode===5)value=123123n;
    else if(q.n>64n)status=67;
    else value=q.n%2n===0n?1n:0n;
  }
  return [BigInt(status),status?0n:value,BigInt(steps),BigInt(peak),BigInt(peak*32),BigInt(depth)];
}
function request(changes={}){return {version:2,mode:0,fuel:4096,frames:64,bytes:2048,n:0n,acc:0n,nodes:[],...changes};}
function encode(q){const b=Buffer.alloc(1600);[q.version,q.mode,q.fuel,q.frames,q.bytes,q.n,q.acc,q.nodes.length].forEach((v,i)=>b.writeBigUInt64LE(BigInt(v),i*8));
  q.nodes.forEach((t,i)=>t.forEach((v,j)=>b.writeBigUInt64LE(BigInt(v),64+i*24+j*8)));return b;}
const cases=[];
function add(q,label){cases.push({q,label});}
for(const n of [0,1,2,7,31,63,64,65,4095])for(const mode of [0,1]){
  for(const fuel of [0,1,n,n+1,4096])for(const frames of [0,1,Math.min(n,64),Math.min(n+1,64),64])
    add(request({mode,n:BigInt(n),fuel,frames,acc:U}),`tail n${n} fuel${fuel} frames${frames}`);
}
const trees=[[],[[9n,64,64]],[[U,64,64],[1n,0,64],[7n,1,0]],
  Array.from({length:64},(_,i)=>[BigInt(i),i?i-1:64,64]),
  Array.from({length:14},(_,i)=>[BigInt(i),i?i-1:64,i?i-1:64])];
// Deterministic varied DAGs, including asymmetric prefixes that reveal child order.
let seed=645;const rand=()=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed;};
for(let i=0;i<40;i++)trees.push(Array.from({length:1+i},(_,j)=>[(BigInt(rand())<<32n)|BigInt(rand()),j&&rand()%3?rand()%j:64,j&&rand()%3?rand()%j:64]));
for(const [ti,nodes] of trees.entries())for(const mode of [2,3])for(const fuel of [0,1,2,3,7,31,127,4096])
  for(const frames of [1,4,16,64])add(request({mode,nodes,n:nodes.length?BigInt(nodes.length-1):64n,fuel,frames}),`tree${ti} fuel${fuel} frames${frames}`);
for(const mode of [0,1,2,3,4])for(const bytes of [0,1,31,32,33,63,64,2047,2048])
  add(request({mode,n:mode===2||mode===3?64n:5n,bytes}),`storage ${mode}/${bytes}`);
for(const fuel of [0,1,16,64,4096])add(request({mode:4,fuel}),`endless${fuel}`);
add(request({mode:5}),'argument evaluation order');
for(let n=0;n<=65;n++)add(request({mode:6,n:BigInt(n)}),`mutual inventory${n}`);
for(const fields of [{version:1},{version:3},{mode:7},{fuel:4097},{frames:65},{bytes:2049},
  {mode:2,n:0n},{mode:2,n:64n,acc:1n},{mode:2,n:0n,nodes:[[1n,0,64]]},
  {mode:2,n:0n,nodes:[[1n,64,1]]},{nodes:[[0n,64,64]]}])add(request(fields),'malformed/bounds');
assert.ok(cases.length<8192);
const input=Buffer.concat(cases.map(({q})=>encode(q)));
fs.writeFileSync(path.join(run,'requests.bin'),input);
fs.writeFileSync(path.join(run,'cases.json'),JSON.stringify(cases,(_,v)=>typeof v==='bigint'?v.toString():v));
const expected=Buffer.alloc(cases.length*48);
cases.forEach(({q},i)=>reference(q).forEach((v,j)=>expected.writeBigUInt64LE(v,i*48+j*8)));
fs.writeFileSync(path.join(run,'expected.bin'),expected);
function compare(binary,label){const r=command(binary,[],input);let mismatches=0,first=[];
  for(let i=0;i<cases.length;i++){const got=r.stdout.subarray(i*48,(i+1)*48),want=expected.subarray(i*48,(i+1)*48);
    if(!got.equals(want)){mismatches++;if(first.length<6)first.push({i,label:cases[i].label,want:[...Array(6)].map((_,j)=>want.readBigUInt64LE(j*8).toString()),got:got.length===48?[...Array(6)].map((_,j)=>got.readBigUInt64LE(j*8).toString()):'missing'});}}
  const result={label,status:r.status,signal:r.signal,observations:r.stdout.length/48,expected:cases.length,mismatches,first,elapsedMs:r.elapsedMs};
  fs.writeFileSync(path.join(run,label+'.json'),JSON.stringify(result,null,2));return result;}
try{
  // Meaningful RED: the valid compiled implementation omits the fuel charge.
  const red=compile(source.replace('put Rstate 1 sub get Rstate 1 1','put Rstate 1 sub get Rstate 1 0'),'red-fuel');
  const redResult=compare(red.path,'red-fuel');assert.ok(redResult.mismatches>0);report.red={...red,...redResult};
  const native=compile(source,'native');report.native=native;
  const result=compare(native.path,'correctness');report.correctness=result;
  assert.equal(result.status,0);assert.equal(result.observations,cases.length);assert.equal(result.mismatches,0,JSON.stringify(result.first));
  const mutations=[['frame-off-by-one','if ge get Rstate 3 get Rstate 5','if gt get Rstate 3 get Rstate 5'],
    ['storage-off-by-one','if gt need get Rstate 6','if ge need get Rstate 6'],
    ['wrong-return','ret add a b','ret sub a b'],
    ['argument-order','x = digits mark 1 mark 2 mark 3','x = digits mark 3 mark 2 mark 1'],
    ['wrong-continuation','result = accum sub n 1 add acc n','result = add 1 accum sub n 1 add acc n'],
    ['child-order','a = tree get Left node\nif eq get Rstate 0 0\nb = tree get Right node','a = tree get Right node\nif eq get Rstate 0 0\nb = tree get Left node']];
  // Tree return mutation has a distinct target from the accumulator.
  mutations[2]=['wrong-return','result = add get Values node add a b','result = sub get Values node add a b'];
  for(const [name,from,to]of mutations){assert.equal(source.split(from).length,2,`unique mutant ${name}`);
    const m=compile(source.replace(from,to),name),r=compare(m.path,name);assert.ok(r.mismatches>0,`${name} survived`);report.mutants.push({...m,...r});}
  const partial=command(native.path,[],input.subarray(0,1601));assert.equal(partial.status,65);assert.equal(partial.stdout.length,48);
  const recovery=[request({mode:4,fuel:1}),request({n:7n}),request({mode:2,n:0n,nodes:[[U,64,64]]})];
  const rr=command(native.path,[],Buffer.concat(recovery.map(encode)));assert.equal(rr.status,0);
  recovery.forEach((q,i)=>reference(q).forEach((v,j)=>assert.equal(rr.stdout.readBigUInt64LE(i*48+j*8),v)));
  report.transport={truncationExit:partial.status,recoveryRequests:recovery.length,status:'PASS'};
  const tutorial=compile(files.slice(0,2).map(p=>fs.readFileSync(p,'utf8')).join('')+fs.readFileSync('tests/universal/recursion/tutorial.om','utf8'),'tutorial');
  const tr=command(tutorial.path);assert.equal(tr.status,0);assert.equal(tr.stdout.toString(),'2016\n80\n');
  report.tutorial={...tutorial,status:tr.status,stdout:tr.stdout.toString()};
  // Full-process measurements on the shared host include validation, pipe I/O and
  // process creation. No isolated-kernel or quiet-machine performance claim.
  const bench=[['tail63',request({n:63n})],['empty',request({n:0n})],
    ['branching',request({mode:2,n:9n,nodes:trees[4].slice(0,10)})]];
  for(const [name,q]of bench){for(let rep=0;rep<5;rep++)for(const iterative of (rep%2?[true,false]:[false,true])){
    const qq={...q,mode:q.mode+(iterative?1:0)},stream=Buffer.concat(Array.from({length:1024},()=>encode(qq)));
    const r=command(native.path,[],stream);assert.equal(r.status,0);assert.equal(r.stdout.length,1024*48);
    const want=reference(qq);for(let i=0;i<1024;i++)want.forEach((v,j)=>assert.equal(r.stdout.readBigUInt64LE(i*48+j*8),v));
    report.measurements.push({workload:name,iterative,rep,requests:1024,totalMs:r.elapsedMs,logicalEntriesPerRequest:Number(want[2]),peakLogicalFrames:Number(want[3]),peakLogicalBytes:Number(want[4]),measurement:'shared-host full process; includes input validation/transport/startup'});
  }}
  report.status='PASS';
}catch(e){report.failure=e.stack;process.exitCode=1;}
finally{fs.writeFileSync(path.join(run,'receipt.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({run,status:report.status,correctness:report.correctness,mutants:report.mutants.length,failure:report.failure}));}
