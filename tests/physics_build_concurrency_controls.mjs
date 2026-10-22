#!/usr/bin/env node
// Acceptance requires exact behavioral failures, never crashes or timeouts.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const outArg=process.argv[2];if(!outArg)throw Error('PRIVATE_OUT required');
fs.mkdirSync(outArg,{recursive:true});const out=fs.mkdtempSync(path.join(fs.realpathSync(outArg),'controls.'));
const text=fs.readFileSync('Makefile','utf8'),hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const prefix=text.indexOf('else\n')+5;const suffix=text.lastIndexOf('\nendif # shared output build dispatch');
if(prefix<5||suffix<0)throw Error('dispatch boundaries missing');
fs.writeFileSync(path.join(out,'no-exclusion.Makefile'),text.slice(prefix,suffix));
const early=path.join(out,'early-release.sh');
fs.writeFileSync(early,fs.readFileSync('tools/with_build_lock.sh','utf8').replace('flock -x 8','flock -x 8\nflock -u 8'));
fs.chmodSync(early,0o755);
fs.writeFileSync(path.join(out,'early-release.Makefile'),text.replace('tools/with_build_lock.sh',early));
fs.writeFileSync(path.join(out,'no-error-cleanup.Makefile'),text.replace('.DELETE_ON_ERROR:\n',''));
const cases=[
 ['serial',['tests/physics_header_identity_test.mjs','Makefile',out,'serial']],
 ...['concurrent','same','refresh'].map(mode=>[mode,['tests/physics_build_concurrency_test.mjs','Makefile',out,mode]]),
 ['real-lifecycle',['tests/physics_build_lifecycle_test.mjs','Makefile',out]],
 ...['no-exclusion','early-release'].map(name=>[name,['tests/physics_header_identity_test.mjs',path.join(out,name+'.Makefile'),out,'concurrent'],'no_mismatched_output_accepted_as_current']),
 ['no-error-cleanup',['tests/physics_build_lifecycle_test.mjs',path.join(out,'no-error-cleanup.Makefile'),out],'failed_partial_object_removed']
];
const results=[];
for(const [label,args,failure] of cases){
 const r=spawnSync(process.execPath,args,{encoding:'utf8',timeout:65000});
 fs.writeFileSync(path.join(out,label+'.log'),r.stdout+r.stderr);
 const pass=failure?r.status===1&&r.stdout.includes(`"name":"${failure}"`)&&r.stdout.includes('"verdict":"FAIL"'):r.status===0;
 const row={label,command:[process.execPath,...args],exit:r.status,signal:r.signal,error:r.error?.message??null,expected_failure:failure??null,pass};results.push(row);console.log(JSON.stringify(row));
 if(!pass)process.exitCode=1;
}
const sources=Object.fromEntries(['Makefile','tools/with_build_lock.sh','tests/physics_build_concurrency_controls.mjs','tests/physics_build_concurrency_test.mjs','tests/physics_build_lifecycle_test.mjs','tests/physics_header_identity_test.mjs'].map(p=>[p,hash(fs.readFileSync(p))]));
fs.writeFileSync(path.join(out,'results.json'),JSON.stringify({sources,results},null,2));
console.log(JSON.stringify({out,pass:results.every(r=>r.pass)}));
