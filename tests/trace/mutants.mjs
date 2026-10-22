// Behavioral mutation runner. Isolated local copies, no edits to the tested tree.
import fs from 'node:fs';import path from 'node:path';import os from 'node:os';import {spawnSync} from 'node:child_process';import assert from 'node:assert/strict';
const targets={'unknown-version':'canonical bytes','unknown-fields':'canonical bytes','frame-digest':'independent frame oracle','recovery-orphan':'unclean shutdown','retention-pin':'quota, age','producer-capacity':'disabled writer no files','loss-attribution':'producer errors stay bounded','query-order':'multi-producer collision','compare-precision':'comparison qualifies','share-allowlist':'canary prompts','share-metadata':'top-level disclosure','loss-filter':'step/status filters','recover-retained-out':'recovery does not resurrect','code-size-qualification':'realization code size','bounded-errors':'producer errors stay bounded','false-persistence':'permission denial refuses','viewer-time':'actual bundled client interactions'};
const mutations=[
 ['unknown-version','schema.mjs',"if(e.schema!==VERSION)","if(false)",'archive'],
 ['unknown-fields','schema.mjs',/export function keys\(o,expected\)\{[^\n]*\}/,'export function keys(o,expected){}','archive'],
 ['frame-digest','archive.mjs',"if(hash(p)!==bytes.subarray(offset+4,offset+36).toString('hex'))","if(false)",'archive'],
 ['recovery-orphan','archive.mjs',"root.names().filter(n=>/^[0-9]{8}\\.seg$/.test(n))","[]",'archive'],
 ['retention-pin','archive.mjs',"if(ix.pins.has(s.name)||","if(false||",'archive'],
 ['producer-capacity','archive.mjs',"this.pending>=this.capacity","false",'archive'],
 ['loss-attribution','archive.mjs',"producer:Number(producer)","producer:65535",'archive'],
 ['query-order','query.mjs',".sort(order)",".sort(()=>0)",'query'],
 ['compare-precision','query.mjs',"['machine','build','config','precision']","['machine','build','config']",'query'],
 ['share-allowlist','share.mjs',"fields.some(f=>!FIELDS.has(f))","false",'sharing'],
 ['share-metadata','share.mjs',"!['recorded_segments_only','retained_segments_only'].includes(result.coverage)","false",'sharing'],
 ['loss-filter','query.mjs',"const loss_disclosures=[...all.filter(e=>e.category==='loss'),","const loss_disclosures=[...events.filter(e=>e.category==='loss'),",'sharing'],
 ['recover-retained-out','archive.mjs',".filter(n=>!retired.has(n))",".filter(()=>true)",'archive'],
 ['code-size-qualification','observers.mjs',"metric('code_bytes',Number(fields.rbytes)","metric('memory',Number(fields.rbytes)",'observers'],
 ['bounded-errors','archive.mjs',"this.errors.length<16&&!this.errors.includes(message)","true",'archive'],
 ['false-persistence','writer.mjs',"persisted:!!a","persisted:true",'archive'],
 ['viewer-time','view-model.mjs',"(from===''||BigInt(e.time.tick)>=BigInt(from))","true",'viewer'],
];
const output=process.env.OMEGA_TRACE_MUTANT_EVIDENCE_DIR??fs.mkdtempSync('build/L489/mutants-');if(process.env.OMEGA_TRACE_MUTANT_EVIDENCE_DIR)fs.mkdirSync(output);let killed=0;
for(const [name,file,from,to,suite]of mutations){const root=fs.mkdtempSync(path.join(os.tmpdir(),'omega-trace-mutant-'));try{
 fs.mkdirSync(path.join(root,'evidence/L489'),{recursive:true});fs.copyFileSync('evidence/L489/synthetic-searchtrace.txt',path.join(root,'evidence/L489/synthetic-searchtrace.txt'));fs.mkdirSync(path.join(root,'build/L489/searchtrace'),{recursive:true});fs.copyFileSync(path.resolve(process.env.OMEGA_TRACE_BUILD_DIR??'build/L489','searchtrace/searchtrace'),path.join(root,'build/L489/searchtrace/searchtrace'));fs.chmodSync(path.join(root,'build/L489/searchtrace/searchtrace'),0o755);
 fs.cpSync('tools/trace',path.join(root,'tools/trace'),{recursive:true});fs.cpSync('tests/trace',path.join(root,'tests/trace'),{recursive:true});const target=path.join(root,'tools/trace',file),source=fs.readFileSync(target,'utf8'),changed=source.replace(from,to);assert.notEqual(changed,source,'unapplied mutant '+name);fs.writeFileSync(target,changed);
 const result=spawnSync(process.execPath,['--test','--test-name-pattern',targets[name],`tests/trace/${suite}.test.mjs`],{cwd:root,encoding:'utf8',timeout:15000,maxBuffer:1024*1024});fs.writeFileSync(path.join(output,`${name}.log`),result.stdout+result.stderr);fs.writeFileSync(path.join(output,`${name}.exit`),String(result.status)+'\n');
 assert.equal(result.error,undefined,name+' test runner error');assert.notEqual(result.status,0,name+' survived');assert.match(result.stdout+result.stderr,/AssertionError|Missing expected exception|ERR_ASSERTION/,name+' must fail a behavioral assertion');killed++;console.log('KILLED '+name+' via '+targets[name]);
 }finally{fs.rmSync(root,{recursive:true,force:true});}}
console.log(JSON.stringify({schema:'omega.trace.mutants.v1',mutants:mutations.length,killed,survived:mutations.length-killed,qualification:'SYNTHETIC_HOST_ONLY'}));
