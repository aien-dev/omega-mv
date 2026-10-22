// External TEST ORACLE ONLY: unchanged C OSC and checker2 APIs, not production compiler.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync,execFileSync} from 'node:child_process';import {hash} from './fixtures.mjs';
const run=process.argv[2],base='69790fb4ad1e803ca79245430745b96129d1dcf4',out=path.join(run,'legacy');fs.mkdirSync(out,{recursive:true});
const cmd=(e,a)=>{let r=spawnSync(e,a,{maxBuffer:16*1024*1024});if(r.status!==0)throw Error(e+' '+r.status+' '+r.stderr);return r.stdout;};
cmd('git',['diff','--exit-code',base,'--','src/compiler','src/checker2','src/sha256.c','src/sha256.h','tests/compiler/progs','tests/compiler/slices','tests/physics0/osc','src/osh']);
let archive=execFileSync('git',['archive',base,'src/compiler','src/checker2','src/sha256.c','src/sha256.h']);let r=spawnSync('tar',['-x','-C',out],{input:archive});assert.equal(r.status,0);
function build(root,name){let compiler=fs.readdirSync(path.join(root,'src/compiler')).filter(p=>p.endsWith('.c')&&p!=='oscc_main.c').map(p=>path.join(root,'src/compiler',p));
 const files=[...compiler,path.join(root,'src/sha256.c'),path.join(root,'src/checker2/c2_ir.c'),path.join(root,'src/checker2/c2_check.c')],exe=path.join(out,name);
 cmd(process.env.CC||'cc',['-std=gnu11','-D_GNU_SOURCE','-Wall','-Wextra','-Werror','-O2','-I'+path.join(root,'src'),'-I'+path.join(root,'src/compiler'),'-I'+path.join(root,'src/checker2'),'-o',exe,'tests/universal/source-aware/legacy.c',...files]);return exe;}
let baseExe=build(out,'base-oracle'),currentExe=build('.','current-oracle'),rows=[];
const fixtures=[...['tests/compiler/progs','tests/compiler/slices','tests/physics0/osc','src/osh'].flatMap(d=>fs.readdirSync(d).filter(x=>x.endsWith('.osc')).map(x=>d+'/'+x)),'tests/universal/arithmetic.osc'];
for(let f of fixtures){let a=cmd(baseExe,[f]),b=cmd(currentExe,[f]);assert.ok(a.equals(b),'lower IR changed '+f);rows.push({source:f,bytes:a.length,irSHA256:hash(a).toString('hex'),byteIdenticalToBase:true,strictDecodeReencode:true,checker2Accepted:true});}
assert.ok(rows.length>=59);fs.writeFileSync(path.join(run,'legacy.json'),JSON.stringify({base,externalTestOnly:true,orchestration:'Node + independent host C API adapter, no production runtime',compiler:cmd(process.env.CC||'cc',['--version']).toString().split('\n')[0],requested:fixtures.length,executed:rows.length,rows},null,2)+'\n');console.log('legacy compatibility '+rows.length+' fixtures byte-identical');
