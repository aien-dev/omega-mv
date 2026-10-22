// Follow-on behavioral mutants. Every kill requires an assertion, never a timeout.
import fs from 'node:fs';import os from 'node:os';import path from 'node:path';
import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
const mutations=[
 ['span-omission','share.mjs',"mandatory_span_fields=['id','parent','run','session','producer'","mandatory_span_fields=['id','parent','run','session'",'T1 spans'],
 ['unclean-ignored','archive.mjs',"if(!ownWriter&&names.has('.writer.lock'))","if(false)",'T2 stale'],
 ['startup-gap','archive.mjs',"writerSession=new Archive(root,{segmentBytes}).handoff()","writerSession=null",'T2 real killed child startup'],
 ['close-race','archive.mjs',"new Set([...namesAtOpen,...root.names()])","new Set(root.names())",'T2 reader racing'],
 ['failure-clears-marker','writer.mjs',"a?.handoff();a=undefined","a?.close();a=undefined",'T2 failed background'],
 ['residual-ignored','archive.mjs',"if(retained.has(n))problems.push(`${n}: tombstoned segment residual; deletion incomplete`);","if(retained.has(n)){}",'T3 real purge cut'],
 ['recovery-fabricates-completeness','archive.mjs',"loss_extent:problems.length?'unknown':'none_detected'","loss_extent:'none_detected'",'T2 stale'],
 ['adopt-failure-clears-marker','archive.mjs',"if(writerSession!==null&&this.lock!==undefined)this.handoff();else this.close();","this.close();",'T2 adopted worker'],
 ['deletion-not-durable','paths.mjs',"fs.unlinkSync(this.target(name));fs.fsyncSync(this.fd)","fs.unlinkSync(this.target(name))",'T3 clean explicit purge'],
];
const output=process.env.OMEGA_TRACE_REVIEW_MUTANTS;assert.ok(output,'explicit fresh evidence directory required');fs.mkdirSync(output);let killed=0;
for(const [name,file,from,to,pattern]of mutations){const root=fs.mkdtempSync(path.join(os.tmpdir(),'L516-mutant-'));try{
 fs.cpSync('tools/trace',path.join(root,'tools/trace'),{recursive:true});fs.cpSync('tests/trace',path.join(root,'tests/trace'),{recursive:true});
 const target=path.join(root,'tools/trace',file),source=fs.readFileSync(target,'utf8');assert.equal(source.split(from).length,2,'exact single mutation '+name);fs.writeFileSync(target,source.replace(from,to));
 const r=spawnSync(process.execPath,['--test','--test-timeout=10000','--test-name-pattern',pattern,'tests/trace/review-fixes.test.mjs'],{cwd:root,encoding:'utf8',timeout:15000,maxBuffer:1024*1024});
 fs.writeFileSync(path.join(output,name+'.log'),(r.stdout??'')+(r.stderr??''));fs.writeFileSync(path.join(output,name+'.exit'),String(r.status)+'\n');
 assert.equal(r.error,undefined,name+' runner failure');assert.notEqual(r.status,0,name+' survived');assert.match(r.stdout+r.stderr,/AssertionError|ERR_ASSERTION/,name+' must fail behavioral assertion');killed++;console.log('KILLED '+name+' via '+pattern);
 }finally{fs.rmSync(root,{recursive:true,force:true});}}
console.log(JSON.stringify({schema:'omega.trace.review-mutants.v1',mutants:mutations.length,killed,survived:mutations.length-killed,qualification:'SYNTHETIC_HOST_ONLY'}));
