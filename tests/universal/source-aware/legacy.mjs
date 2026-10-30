// External TEST ORACLE ONLY: unchanged C OSC and checker2 APIs, not production compiler.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync,execFileSync} from 'node:child_process';import {hash} from './fixtures.mjs';
const run=process.argv[2],base='69790fb4ad1e803ca79245430745b96129d1dcf4',out=path.join(run,'legacy');fs.mkdirSync(out,{recursive:true});
const cmd=(e,a)=>{let r=spawnSync(e,a,{maxBuffer:16*1024*1024});if(r.status!==0)throw Error(e+' '+r.status+' '+r.stderr);return r.stdout;};
// The legacy paths must still hold the base's code. Since base, main changed them in exactly two ways, each
// checked byte for byte here; anything else (an added, renamed or otherwise edited file) fails:
// (1) the Apache-2.0 SPDX header (HDR) put in front of a file, base bytes unchanged after it, and committed
//     .crumb metadata; (2) the SHA-256 cutover (#776): src/sha256.c deleted, src/sha256.h reduced to the
//     include of the Omega-generated API. The current oracle is then built against that generated SHA-256
//     (src/omega_om_units.c), so the byte-identical IR comparison below also covers the cutover.
const HDR=['/* SPDX-License-Identifier: Apache-2.0',' * Copyright 2026 Drake Stapleton (AIEN / aien-dev)',' *',' * Licensed under the Apache License, Version 2.0 (the "License");',' * you may not use this file except in compliance with the License.',' * You may obtain a copy of the License at',' *',' *     http://www.apache.org/licenses/LICENSE-2.0',' *',' * Unless required by applicable law or agreed to in writing, software',' * distributed under the License is distributed on an "AS IS" BASIS,',' * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.',' * See the License for the specific language governing permissions and',' * limitations under the License.',' */',''].join('\n');
const legacyPaths=['src/compiler','src/checker2','src/sha256.c','src/sha256.h','tests/compiler/progs','tests/compiler/slices','tests/physics0/osc','src/osh'];
const changed=cmd('git',['diff','--no-renames','--name-status','-z',base,'--',...legacyPaths]).toString().split('\0').filter(Boolean);
let cutover=false;
for(let i=0;i<changed.length;i+=2){const st=changed[i],f=changed[i+1];
 if(path.basename(f)==='.crumb'&&st==='M')continue;
 if(f==='src/sha256.c'){assert.equal(st,'D','legacy path changed: '+st+' '+f);cutover=true;continue;}
 assert.equal(st,'M','legacy path changed: '+st+' '+f);
 const cur=fs.readFileSync(f,'utf8');
 if(f==='src/sha256.h'){assert.ok(cur.startsWith(HDR)&&/#include "omega_api_decl\.h"/.test(cur)&&!/[{;]/.test(cur.replace(/\/\*[\s\S]*?\*\//g,'')),'src/sha256.h is not the generated-API include');continue;}
 const old=cmd('git',['show',base+':'+f]).toString('utf8'),rest=cur.startsWith(HDR)?cur.slice(HDR.length):null;
 assert.ok(rest!==null&&(rest==='\n'+old||rest===old),'legacy path changed beyond the SPDX header: '+f);}
assert.ok(cutover===fs.existsSync('src/omega_om_units.c')&&cutover!==fs.existsSync('src/sha256.c'),'SHA-256 cutover state inconsistent');
let archive=execFileSync('git',['archive',base,'src/compiler','src/checker2','src/sha256.c','src/sha256.h']);let r=spawnSync('tar',['-x','-C',out],{input:archive});assert.equal(r.status,0);
function build(root,name){let compiler=fs.readdirSync(path.join(root,'src/compiler')).filter(p=>p.endsWith('.c')&&p!=='oscc_main.c').map(p=>path.join(root,'src/compiler',p));
 const sha=fs.existsSync(path.join(root,'src/sha256.c'))?[path.join(root,'src/sha256.c')]:[path.join(root,'src/omega_om_units.c'),'-pthread'];
 const files=[...compiler,...sha,path.join(root,'src/checker2/c2_ir.c'),path.join(root,'src/checker2/c2_check.c')],exe=path.join(out,name);
 cmd(process.env.CC||'cc',['-std=gnu11','-D_GNU_SOURCE','-Wall','-Wextra','-Werror','-O2','-I'+path.join(root,'src'),'-I'+path.join(root,'src/compiler'),'-I'+path.join(root,'src/checker2'),'-o',exe,'tests/universal/source-aware/legacy.c',...files]);return exe;}
let baseExe=build(out,'base-oracle'),currentExe=build('.','current-oracle'),rows=[];
const fixtures=[...['tests/compiler/progs','tests/compiler/slices','tests/physics0/osc','src/osh'].flatMap(d=>fs.readdirSync(d).filter(x=>x.endsWith('.osc')).map(x=>d+'/'+x)),'tests/universal/arithmetic.osc'];
for(let f of fixtures){let a=cmd(baseExe,[f]),b=cmd(currentExe,[f]);assert.ok(a.equals(b),'lower IR changed '+f);rows.push({source:f,bytes:a.length,irSHA256:hash(a).toString('hex'),byteIdenticalToBase:true,strictDecodeReencode:true,checker2Accepted:true});}
assert.ok(rows.length>=59);fs.writeFileSync(path.join(run,'legacy.json'),JSON.stringify({base,externalTestOnly:true,orchestration:'Node + independent host C API adapter, no production runtime',compiler:cmd(process.env.CC||'cc',['--version']).toString().split('\n')[0],requested:fixtures.length,executed:rows.length,rows},null,2)+'\n');console.log('legacy compatibility '+rows.length+' fixtures byte-identical');
