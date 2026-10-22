#!/usr/bin/env node
// Executes expected failures as first-class results; accepts only the named assertion.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
const outArg=process.argv[2];if(!outArg)throw Error('private output directory required');
fs.mkdirSync(outArg,{recursive:true});const out=fs.mkdtempSync(path.join(fs.realpathSync(outArg),'matrix.'));
const sources={baseline:['85d8343fa04647b198e3ddda00ad8e448837f9ca',null],
    pr546:['8a4f15a6abedd3c21c17bf2f7f033d94fbab57ea','85f154610350ed3cf24f1705a08da774985d89dd5c8d43a5bf3b61b8f29b1066'],
    pr495:['a1896dcf68a899fe58ffbc32559fd4b3fa5bd4b3','d53730d0eddb1da9d568a010db0bafd9f8d5eab3407ae15007bac66d14aa0af8']};
for(const [name,[ref,pin]] of Object.entries(sources)){
    const r=spawnSync('git',['show',ref+':Makefile'],{encoding:'utf8'});if(r.status!==0)throw Error(r.stderr);
    const hash=crypto.createHash('sha256').update(r.stdout).digest('hex');if(pin&&hash!==pin)throw Error('source pin mismatch');
    fs.writeFileSync(path.join(out,name+'.Makefile'),r.stdout);console.log(JSON.stringify({name,ref,sha256:hash}));
}
const candidate=fs.readFileSync(path.join(out,'pr495.Makefile'),'utf8');
const mutants={
    'no-header':[candidate.replace('$(PHYSICS_HDR_OBJS) $(RX_R12_SILICON)','$(RX_R12_SILICON)'),'serial','directory_switch_older_header'],
    'no-diff':[candidate.replace('diff=$$(git -C "$(PHYSICS_DIR)" diff --no-ext-diff HEAD 2>/dev/null | sha256sum | cut -d\' \' -f1);','diff=disabled;'),'serial','dirty_header_older_mtime'],
    'always-write':[candidate.replace('if cmp -s $@.tmp.$$$$ $@; then','if false; then'),'serial','valid_cache_hit'],
    'incomplete-stamp':[candidate.replace("commit=%s\\ndiff_sha256=%s\\n'","commit=%s\\nincomplete=%s\\n'"),'refresh','complete_unchanged_stamp']
};
for(const [name,[text]] of Object.entries(mutants)) {if(text===candidate)throw Error('mutation not applied');fs.writeFileSync(path.join(out,name+'.Makefile'),text);}
const cases=[['baseline','serial','directory_switch_older_header'],['pr546','serial','directory_switch_older_header'],
    ['pr495','serial',null],['pr495','refresh',null],['pr495','concurrent','no_mismatched_output_accepted_as_current'],
    ...Object.entries(mutants).map(([name,[,mode,failure]])=>[name,mode,failure])];
const results=[];
for(const [name,mode,failure] of cases){
    const r=spawnSync(process.execPath,['tests/physics_header_identity_test.mjs',path.join(out,name+'.Makefile'),out,mode],{encoding:'utf8',timeout:45000});
    const label=name+'-'+mode;fs.writeFileSync(path.join(out,label+'.log'),r.stdout+r.stderr);
    const actual=(r.stdout.match(/"verdict":"FAIL","name":"([^"]+)"/)||[])[1];
    const pass=failure?r.status===1&&actual===failure:r.status===0&&r.stdout.includes('PHYSICS_HEADER_IDENTITY_PASS');
    results.push({label,exit:r.status,expected_failure:failure,actual_failure:actual??null,pass});
    console.log(JSON.stringify(results.at(-1)));if(!pass)process.exitCode=1;
}
fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(results,null,2));
console.log(JSON.stringify({output:out,pass:results.every(x=>x.pass),candidate_concurrency:'FAIL_REPRODUCED_NO_PRODUCTION_FIX',driver_chip:'NOT_RUN'}));
