#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
if(!process.argv[2])throw Error('PRIVATE_OUT required');
fs.mkdirSync(process.argv[2],{recursive:true});
const out=fs.mkdtempSync(path.join(fs.realpathSync(process.argv[2]),'safety-controls.'));
const source=fs.readFileSync('tools/with_build_lock.sh','utf8');
const start=source.indexOf('lock=$canonical.omega-build.lock\n'),end=source.indexOf('flock -x 8\n');
if(start<0||end<start)throw Error('mutation boundary absent');
const unsafe=source.slice(0,start)+'exec 8>"$canonical.omega-build.lock"\n'+source.slice(end);
const append=source.slice(0,start)+'exec 8>>"$canonical.omega-build.lock"\n'+source.slice(end);
const truncate=source.replace('exec 8>>"$lock"','exec 8>"$lock"');
const results=[];
for(const [label,text,mode,expected] of [
 ['production',source,'all',[]],
 ['unsafe-open',unsafe,'linked',['direct-symlink','direct-hardlink']],
 ['unchecked-append',append,'linked',['direct-symlink','direct-hardlink']],
 ['truncate-regular',truncate,'all',['direct-nonempty']]
]){
 const helper=path.join(out,label+'.sh');fs.writeFileSync(helper,text);
 // Mutant paths exercise direct helper. Public make path always uses production.
 const r=spawnSync(process.execPath,['tests/build_lock_file_safety_test.mjs',out,helper,mode],{encoding:'utf8',timeout:25000});
 fs.writeFileSync(path.join(out,label+'.log'),r.stdout+r.stderr);
 const rows=r.stdout.split('\n').flatMap(line=>{try{const x=JSON.parse(line);return x.verdict?[x]:[];}catch{return [];}});
 const failures=rows.filter(x=>x.verdict==='FAIL');
 const pass=!r.error&&!r.signal&&r.status===(expected.length?1:0)&&rows.length===(mode==='linked'?2:18)&&JSON.stringify(failures.map(x=>x.name))===JSON.stringify(expected)&&failures.every(x=>x.assertion==='safe_lock_leaf_preserves_bytes_and_command_boundary'&&x.exit===0&&x.signal===null&&x.error===null);
 const row={label,exit:r.status,signal:r.signal,error:r.error?.message??null,expected_failures:expected,actual_failures:failures.map(x=>x.name),pass};results.push(row);console.log(JSON.stringify(row));
}
fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(results,null,2));
console.log(JSON.stringify({out,pass:results.every(x=>x.pass)}));
if(results.some(x=>!x.pass))process.exitCode=1;
