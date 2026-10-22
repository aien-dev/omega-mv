// Compiled behavioral controls: normal completion and exact named failures only.
import {spawnSync} from 'node:child_process';
import {mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve,join} from 'node:path';
const [directory]=process.argv.slice(2);
if (!directory) throw Error('usage: node exit_numeric_priority_mutants.mjs FRESH_OUT');
const out=resolve(directory); mkdirSync(out,{mode:0o700});
const source=readFileSync('src/osh/host/osh_builtin.c','utf8');
const old=spawnSync('git',['show','f2dbbb241cd9b3c0302d1bc3630d61f617d12c2c:src/osh/host/osh_builtin.c'],{encoding:'utf8'});
if(old.status!==0) throw Error('baseline unavailable');
const start=source.indexOf('static int bi_exit('), end=source.indexOf('\nint osh_builtin_run(',start);
const oldStart=old.stdout.indexOf('static int bi_exit('), oldEnd=old.stdout.indexOf('\nint osh_builtin_run(',oldStart);
if([start,end,oldStart,oldEnd].some(x=>x<0)) throw Error('missing function boundary');
const final='if (in_parent) { s->exit_requested = 1; s->exit_status = st; }';
if(source.split(final).length!==2) throw Error('nonunique exit ownership anchor');
const variants=[
 ['count-first',source.slice(0,start)+old.stdout.slice(oldStart,oldEnd)+source.slice(end),60,true],
 ['invalid-extra-continues',source.replace(final,'if (in_parent && !(c->nargv > 2 && st == 2)) { s->exit_requested = 1; s->exit_status = st; }'),48,false],
];
for(const [name,text,count,pipelines] of variants){
 const path=join(out,name+'.c'); writeFileSync(path,text);
 for(const leg of ['plain','sanitizer']) {
 const key=name+'-'+leg, buildDir=join(out,key);
 const target=leg==='plain'?'osh':join(buildDir,'osh/osh_numeric_priority_asan');
 const executable=leg==='plain'?'osh':'osh_numeric_priority_asan';
 const args=['-s','-j2',`OUT_DIR=${buildDir}`,`OSH_BIN_SRCS=$(OSH_DIR)/osh_units.c $(filter-out src/osh/host/osh_builtin.c src/osh/host/osh_codes.c,$(OSH_PROG_HOSTSRCS)) ${path} src/osh/host/osh_codes.c $(OSC_LIB) src/runtime/rx_caproot.c`,target];
 const build=spawnSync('make',args,{encoding:'utf8',timeout:180000});
 writeFileSync(join(out,key+'-build.json'),JSON.stringify({command:['make',...args],status:build.status,signal:build.signal,error:build.error?.message??null,stdout:build.stdout,stderr:build.stderr},null,2)+'\n');
 if(build.error||build.signal||build.status!==0) throw Error(name+': build failure is not a kill');
 // Each child command retains its own20s bound. The full sanitizer corpus
 // takes over90s on this host; an outer runner timeout is never a kill.
 const r=spawnSync(process.execPath,['tests/osh/e2e/exit_numeric_priority.mjs',join(buildDir,'osh',executable),join(out,key+'-cases')],{encoding:'utf8',timeout:240000});
 writeFileSync(join(out,key+'-test.log'),(r.stdout??'')+(r.stderr??''));
 writeFileSync(join(out,key+'-test.json'),JSON.stringify({status:r.status,signal:r.signal,error:r.error?.message??null})+'\n');
 if(r.error||r.signal||r.status!==1) throw Error(name+': abnormal execution is not a kill');
 const rows=JSON.parse(readFileSync(join(out,key+'-cases/results.json'))).rows;
 const failures=rows.filter(x=>!x.ok);
 const extraNames=['bad-extra','empty-extra','plus-only-extra','minus-only-extra','overflow-positive-extra','overflow-negative-extra','signed-bad-extra','many-invalid-extra'];
 const expected=rows.filter(x=>extraNames.includes(x.name)||(pipelines&&['pipeline-first-invalid','pipeline-last-invalid'].includes(x.name)));
 if(failures.length!==count||expected.length!==count||expected.some(x=>x.ok)) throw Error(name+': wrong failure set; no kill');
 console.log(`PASS ${key}: compiled, ${count} exact numeric-priority failures, all other assertions pass`);
 }
}
console.log('EXIT_NUMERIC_PRIORITY_MUTANTS: 2 variants killed in both plain and sanitizer builds');
