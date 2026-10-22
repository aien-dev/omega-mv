// SPDX-License-Identifier: AGPL-3.0-only
import fs from 'node:fs';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
const root='build/math-native',evidence='evidence/math-native';
fs.mkdirSync(`${evidence}/mutants`,{recursive:true});
const core=fs.readFileSync('seed/math/core.om','utf8'),cli=fs.readFileSync('seed/math/cli.om','utf8');
const hash=b=>createHash('sha256').update(b).digest('hex');
function elf(b){if(b.length<120||b.subarray(0,4).toString('hex')!=='7f454c46'||b[4]!==2||b[5]!==1||b.readUInt16LE(18)!==183||b.readUInt16LE(16)!==2)throw Error('not a nonempty AArch64 ELF executable');}
function build(name,source){
 fs.writeFileSync(`${root}/${name}.om`,source);
 const r=spawnSync(`${root}/omc1`,[],{input:source,maxBuffer:8*1024*1024});
 fs.writeFileSync(`${root}/${name}.compile.stderr`,r.stderr);
 if(r.status!==0)throw Error(`${name} compiler failure ${r.status}: ${r.stderr}, NOT a mutation detection`);
 elf(r.stdout);fs.writeFileSync(`${root}/${name}`,r.stdout,{mode:0o755});
 return {exe:`${root}/${name}`,sourceHash:hash(source),binaryHash:hash(r.stdout),compileStatus:r.status,validAarch64Elf:true};
}
function run(exe,input){const r=spawnSync(exe,[],{input,encoding:'utf8',timeout:60000,maxBuffer:8*1024*1024});if(r.error||r.signal)throw Error(`run failed ${r.error} ${r.signal}`);return r;}
let log=[];
const unit=build('unit',core+fs.readFileSync('seed/math/unit.om','utf8'));
let r=run(unit.exe,'');
fs.writeFileSync(`${evidence}/unit.log`,r.stdout);
if(r.status!==0||r.stdout.trim().split('\n').length!==23||!r.stdout.trim().split('\n').every(l=>l.endsWith(' 0')))throw Error(`module units failed: ${r.status} ${r.stdout}`);
log.push({kind:'module units',...unit,requested:23,executed:23,status:r.status});
const cases=JSON.parse(fs.readFileSync('seed/math/cases.json'));
const mutations=[
 ['carry','carry = div v 10000','carry = 0'],
 ['multiply','mul bg a i bg b j','add bg a i bg b j'],
 ['gcd','bc 25 a\nbc 26 b','bset 25 1\nret\nbc 26 b'],
 ['negative','put sg r sub 1 get sg a','put sg r get sg a'],
 ['sum','badd 3 0 1','bc 3 0'],
 ['derivative','cs p i rmul cg a add i 1 rint add i 1','cs p i cg a add i 1'],
 ['antiderivative','cs p add i 1 rdiv cg a i rint add i 1','cs p add i 1 cg a i'],
 ['substitution','r = radd cg a i rmul r cg b 0','r = radd cg a i r'],
 ['solve','ret pconst rdiv rneg cg p 0 cg p 1','ret pconst rdiv cg p 0 cg p 1'],
 ['cancel','fn pdiv a ulong b ulong to ulong','fn pdiv a ulong b ulong to ulong\nif eq a b\nret 1\nend'],
 ['power_precedence','p = parse 30 add depth 1','p = parse 40 add depth 1'],
 ['linear','put second 0 pconst rdiv y d','put second 0 pconst rdiv x d'],
 ['poly_gcd','ret pdiv p pconst cg p deg p','ret 1']
];
for(const [name,from,to] of mutations){
 if(!core.includes(from))throw Error('mutation anchor missing '+name);
 // The gcd mutation replaces the entire function, avoiding dead-source compile refusal.
 let src=core.replace(from,to);
 if(name==='gcd')src=core.replace(/fn bgcd a ulong b ulong to void[\s\S]*?\nfn ralloc/, 'fn bgcd a ulong b ulong to void\nbset 25 1\nend\nfn ralloc');
 const b=build('mutant-'+name,src+cli),r=run(b.exe,cases.map(c=>c[0]).join('\n')+'\n');
 const got=r.stdout.trimEnd().split('\n');
 fs.writeFileSync(`${evidence}/mutants/${name}.log`,cases.map(([input,want],i)=>JSON.stringify({input,want,got:got[i]})).join('\n')+'\n');
 // Compiler error, invalid ELF, crash, timeout, trap, or missing output NEVER count as detection.
 if(r.status!==0||got.length!==cases.length)throw Error(`mutant ${name} failed to execute full cases: ${r.status} ${got.length}`);
 const failures=cases.flatMap(([input,want],i)=>want===got[i]?[]:[{input,want,got:got[i]}]);
 if(!failures.length)throw Error('surviving mutant '+name);
 log.push({kind:'wrong-rule mutant',name,...b,requested:cases.length,executed:got.length,detectedBy:failures});
}
// Streaming, reset after malformed requests, no-newline EOF, and process bounds.
const exe=`${root}/omega-math`;
for(const [name,input,want,status] of [
 ['long stream',('eval 1'+' '.repeat(4089)+'\n').repeat(300),'OK 1\n'.repeat(300),0],
 ['recover',('eval 1/0\neval x/x\neval 7/9\n').repeat(100),'REFUSED 2\nREFUSED 3\nOK 7/9\n'.repeat(100),0],
 ['EOF','eval 2+3','OK 5\n',0],['empty input','','',0],
 ['transport bound','x'.repeat(65536),'REFUSED 4\n',1],
 ['oversize recovery','x'.repeat(5000)+'\neval 2\n','REFUSED 4\nOK 2\n',0]
]){
 const r=run(exe,input),ok=r.status===status&&r.stdout===want;
 log.push({kind:'stream',name,inputHash:hash(input),inputBytes:Buffer.byteLength(input),requested:want.split('\n').length-1,executed:r.stdout.split('\n').length-1,expectedStatus:status,status:r.status,ok});
 if(!ok)throw Error('stream failed '+name);
}
// Build twice and run with a scrubbed environment: same bytes and same output.
const again=build('rebuild',core+cli);
if(!fs.readFileSync(exe).equals(fs.readFileSync(again.exe)))throw Error('nonreproducible binary');
r=spawnSync(exe,[],{input:'expand (x+1)^5\n',encoding:'utf8',env:{},timeout:10000});
if(r.status!==0||r.stdout!=='OK 1+5*x+10*x^2+10*x^3+5*x^4+x^5\n')throw Error('environment dependency');
log.push({kind:'determinism',...again,status:r.status,emptyEnvironment:true});
fs.writeFileSync(`${evidence}/checks.json`,JSON.stringify(log,null,2)+'\n');
console.log(JSON.stringify({moduleTests:23,compiledValidMutants:mutations.length,detectedMutants:mutations.length,mutantCases:mutations.length*cases.length,streamTests:6,deterministic:true}));
