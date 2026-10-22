#!/usr/bin/env node
// Real tiny header-reading objects, real make recursion, kernel lock waiters.
import fs from 'node:fs';import path from 'node:path';import net from 'node:net';import crypto from 'node:crypto';
import {spawn,spawnSync} from 'node:child_process';import {setTimeout as delay} from 'node:timers/promises';
const [makeArg,outArg]=process.argv.slice(2);if(!makeArg||!outArg)throw Error('MAKEFILE PRIVATE_OUT required');
const source=process.cwd();fs.mkdirSync(outArg,{recursive:true});
const root=fs.mkdtempSync(path.join(fs.realpathSync(outArg),'lifecycle.'));
const out=path.join(root,'objects'), target=path.join(out,'m16_native.o');
fs.mkdirSync(out);fs.mkdirSync(path.join(root,'mk'));
fs.copyFileSync(makeArg,path.join(root,'Makefile'));
fs.copyFileSync('mk/quiet.mk',path.join(root,'mk/quiet.mk'));
for(const name of ['tools','src'])fs.symlinkSync(path.join(source,name),path.join(root,name));
const run=(cmd,args,opts={})=>{const r=spawnSync(cmd,args,{encoding:'utf8',...opts});if(r.status!==0)throw Error(`${cmd}: ${r.status} ${r.stderr}`);return r.stdout;};
function check(ok,name){console.log(JSON.stringify({name,verdict:ok?'PASS':'FAIL'}));if(!ok)throw Error(name);}
console.log(JSON.stringify({root,makefile_sha256:crypto.createHash('sha256').update(fs.readFileSync(makeArg)).digest('hex')}));
const trees=['a','b'].map((name,i)=>{
    const p=path.join(root,name);fs.mkdirSync(path.join(p,'m16'),{recursive:true});fs.mkdirSync(path.join(p,'nvrm'));
    fs.writeFileSync(path.join(p,'m16/m16_native.h'),`#define VALUE ${i+11}\n`);
    fs.writeFileSync(path.join(p,'m16/m16_native.c'),'#include "m16_native.h"\nint physics_value(void){return VALUE;}\n');
    fs.writeFileSync(path.join(p,'nvrm/nvrm.c'),'int nvrm_fixture(void){return 0;}\n');
    run('git',['init','-q',p]);run('git',['-C',p,'add','.']);run('git',['-C',p,'-c','user.name=Fixture','-c','user.email=f@invalid','-c','commit.gpgsign=false','-c','core.hooksPath=/dev/null','commit','-qm','fixture']);
    for(const f of ['m16/m16_native.c','m16/m16_native.h','nvrm/nvrm.c'])fs.utimesSync(path.join(p,f),new Date('2000-01-01Z'),new Date('2000-01-01Z'));
    return p;
});
let server;
async function gate(){
    const sock=`/tmp/L537-${process.pid}-${crypto.randomBytes(4).toString('hex')}.sock`;
    const arrivals=[],connections=[];server=net.createServer(s=>{connections.push(s);let text='';s.on('data',b=>{text+=b;if(text.includes('\n'))arrivals.push(JSON.parse(text));});});
    await new Promise(r=>server.listen(sock,r));
    return {sock,arrivals,release(){connections.forEach(c=>c.end('release\n'));},close(){server.close();}};
}
const worker=path.join(root,'worker.mjs');
fs.writeFileSync(worker,`import fs from 'node:fs';import net from 'node:net';import {spawnSync} from 'node:child_process';
const args=process.argv.slice(2);const source=args[args.indexOf('-c')+1],out=args[args.indexOf('-o')+1];
if(process.env.L537_GATE && (args.includes('-c')||args[0]==='parallel')) await new Promise((resolve,reject)=>{const c=net.createConnection(process.env.L537_GATE,()=>c.write(JSON.stringify({pid:process.pid,source,args})+'\\n'));c.on('data',()=>{c.end();resolve();});c.on('error',reject);c.on('end',()=>reject(Error('barrier disconnected')));});
if(args[0]==='parallel')process.exit(0);
if(args.includes('-c')&&process.env.L537_FAIL){fs.writeFileSync(out,'partial invalid object');process.exit(1);}
const r=spawnSync('cc',args,{stdio:'inherit'});process.exit(r.status??1);
`);
const compiler=path.join(root,'cc');fs.writeFileSync(compiler,`#!/bin/sh\nexec '${process.execPath}' '${worker}' "$@"\n`);fs.chmodSync(compiler,0o755);
fs.writeFileSync(path.join(root,'mk/lifecycle.mk'),`.PHONY: nested parallel-a parallel-b fail-recipe\nnested:\n\t+$(MAKE) --no-print-directory parallel-a parallel-b\nparallel-a parallel-b:\n\t@${process.execPath} ${worker} parallel $@\nfail-recipe:\n\t@false\n`);
function build(p,goals=[target],env={},outputDir=out){
    const child=spawn('make',['--no-print-directory','-j2',`OUT_DIR=${outputDir}`,`PHYSICS_DIR=${p}`,`CC=${compiler}`,...goals],{cwd:root,env:{...process.env,...env}});
    let output='';child.stdout.on('data',b=>output+=b);child.stderr.on('data',b=>output+=b);
    const done=new Promise((resolve,reject)=>{child.on('error',reject);child.on('close',code=>{fs.appendFileSync(path.join(root,'make.log'),output);resolve({code,output});});});
    return {child,done};
}
async function waitFor(f){while(!f())await delay(10);}
async function waiting(){const ino=fs.statSync(out+'.omega-build.lock').ino;await waitFor(()=>fs.readFileSync('/proc/locks','utf8').split('\n').some(l=>l.includes('->')&&l.includes(':'+ino+' ')));}
function value(object=target){
    const main=path.join(root,'main.c'),exe=path.join(root,'probe');fs.writeFileSync(main,'#include <stdio.h>\nint physics_value(void);int main(void){printf("%d\\n",physics_value());return 0;}\n');
    run('cc',[main,object,'-o',exe]);return Number(run(exe,[]));
}
const watchdog=setTimeout(()=>{console.error('FAIL lifecycle deadline');process.exit(2);},45000);
try{
    check((await build(trees[0],['quietlock']).done).code===0,'quiet_bootstrap');
    check((await build(trees[0]).done).code===0&&value()===11,'real_header_object_a');
    const before=fs.statSync(target).mtimeNs??fs.statSync(target).mtimeMs;
    check((await build(trees[0]).done).code===0&&(fs.statSync(target).mtimeNs??fs.statSync(target).mtimeMs)===before,'real_noop_cache_hit');
    check((await build(trees[1]).done).code===0&&value()===12,'real_header_object_b_older_mtime');
    fs.unlinkSync(target);
    check((await build(trees[0],[target],{L537_FAIL:'1'}).done).code!==0,'compiler_failure_propagates');
    check(!fs.existsSync(target),'failed_partial_object_removed');
    check((await build(trees[0]).done).code===0&&value()===11,'compiler_error_recovery');
    check((await build(trees[0],['fail-recipe']).done).code!==0,'recipe_failure_releases_lock');
    check((await build(trees[0]).done).code===0,'recipe_error_recovery');
    const parallel=await gate();const nested=build(trees[0],['nested'],{L537_GATE:parallel.sock});
    await waitFor(()=>parallel.arrivals.length===2);
    check(parallel.arrivals.length===2,'nested_jobserver_two_recipes_overlap');parallel.release();
    const nr=await nested.done;parallel.close();
    check(nr.code===0&&!/jobserver unavailable|resetting jobserver/.test(nr.output),'nested_make_jobserver_preserved');
    // A different output directory must make progress while A is paused.
    fs.unlinkSync(target);const separate=await gate();
    const paused=build(trees[0],[target],{L537_GATE:separate.sock});
    await waitFor(()=>separate.arrivals.length===1);
    const other=path.join(root,'independent'),otherObject=path.join(other,'m16_native.o');
    check((await build(trees[1],[otherObject],{},other).done).code===0&&value(otherObject)===12,'distinct_output_directory_progresses');
    separate.release();check((await paused.done).code===0,'independent_build_releases_normally');separate.close();
    // Different spellings of one output must still use the same lock inode.
    fs.unlinkSync(target);const alias=path.join(root,'output-alias');fs.symlinkSync(out,alias);
    const aliasGate=await gate(),aliasA=build(trees[0],[target],{L537_GATE:aliasGate.sock});
    await waitFor(()=>aliasGate.arrivals.length===1);
    const aliasB=build(trees[1],[path.join(alias,'m16_native.o')],{},alias);await waiting();
    check(!fs.existsSync(target),'output_alias_waits_on_same_inode');aliasGate.release();await aliasA.done;
    check((await aliasB.done).code===0&&value()===12,'output_alias_publishes_matching_real_object');aliasGate.close();
    // Kill the actual inner make while its compiler still owns inherited fd 8.
    fs.unlinkSync(target);const g=await gate();const a=build(trees[0],[target],{L537_GATE:g.sock});
    await waitFor(()=>g.arrivals.length===1);
    let inner;await waitFor(()=>{const children=fs.readFileSync(`/proc/${a.child.pid}/task/${a.child.pid}/children`,'utf8').trim().split(/\s+/).filter(Boolean);inner=children.find(p=>fs.readFileSync(`/proc/${p}/comm`,'utf8').trim()==='make');return !!inner;});
    process.kill(Number(inner),'SIGKILL');await waitFor(()=>a.child.exitCode!==null);check(a.child.exitCode!==0,'killed_make_failure_reported');
    const b=build(trees[1]);await waiting();
    check(g.arrivals.length===1&&!fs.existsSync(target),'orphan_compiler_retains_exclusion');
    g.release();await a.done;const br=await b.done;g.close();
    check(br.code===0&&value()===12,'termination_recovers_without_late_a_overwrite');
    const stamp=fs.readFileSync(path.join(out,'physics.stamp'),'utf8');
    check(stamp.includes('dir='+trees[1]+'\n'),'real_object_matches_final_stamp');
    console.log('PHYSICS_BUILD_LIFECYCLE_PASS');
}catch(e){console.error('FAIL '+e.message);process.exitCode=1;}finally{clearTimeout(watchdog);if(server)server.close();}
