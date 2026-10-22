#!/usr/bin/env node
// L537 serialization oracle; original L532 test is retained unchanged.
// Host-only structural oracle. Compiler markers are never linked or executed.
// The supplied Makefile is immutable; fixtures and -MMD closure are private.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import net from 'node:net';
import {setTimeout as delay} from 'node:timers/promises';
import {spawn, spawnSync} from 'node:child_process';
const [makeArg, outArg, mode = 'serial'] = process.argv.slice(2);
if (!makeArg || !outArg || !['serial', 'concurrent', 'refresh', 'same'].includes(mode)) {
    console.error('usage: physics_header_identity_test.mjs MAKEFILE PRIVATE_OUTDIR [serial|concurrent|refresh|same]');
    process.exit(2);
}
const makefile = fs.realpathSync(makeArg);
fs.mkdirSync(outArg, {recursive:true});
const root = fs.mkdtempSync(path.join(fs.realpathSync(outArg), 'headers.'));
const objects = path.join(root, 'objects'); fs.mkdirSync(objects);
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
console.log(JSON.stringify({root, mode, makefile, makefile_sha256:sha(fs.readFileSync(makefile))}));
const run = (cmd,args,opts={}) => {
    const r = spawnSync(cmd,args,{encoding:'utf8',...opts});
    if (r.status !== 0) throw Error(`${cmd} exit=${r.status}: ${r.stderr || r.stdout}`);
    return r.stdout;
};
const git = (p,...args) => run('git',['-C',p,...args]);
function commit(p) {
    git(p,'add','.');
    git(p,'-c','user.name=Fixture','-c','user.email=fixture@invalid','-c','commit.gpgsign=false',
        '-c','core.hooksPath=/dev/null','commit','--allow-empty','-qm','fixture');
}
const old = p => fs.utimesSync(p,new Date('2000-01-01Z'),new Date('2000-01-01Z'));
const trees = ['a','b'].map(name => {
    const p=path.join(root,`physics-${name}`); fs.mkdirSync(path.join(p,'m16'),{recursive:true});
    fs.mkdirSync(path.join(p,'nvrm'));
    fs.writeFileSync(path.join(p,'m16/m16_native.h'),`/* synthetic header ${name} */\n`);
    fs.writeFileSync(path.join(p,'m16/m16_native.c'),'/* synthetic input */\n');
    fs.writeFileSync(path.join(p,'nvrm/nvrm.c'),'/* synthetic input */\n');
    git(p,'-c','core.hooksPath=/dev/null','init','-q'); commit(p);
    for(const f of ['m16/m16_native.h','m16/m16_native.c','nvrm/nvrm.c']) old(path.join(p,f));
    return p;
});
const [a,otherB]=trees, b=mode==='same'?a:otherB, target=path.join(objects,'omega_accelerator_world.o');
const dep=path.join(objects,'omega_accelerator_world.d'), stamp=path.join(objects,'physics.stamp');
// Represents the real header-reading existing Omega object, using a compiler-style
// dependency closure. Its recipe still comes exclusively from supplied Makefile.
fs.writeFileSync(dep,`${target}: src/omega_accelerator_world.c ${a}/m16/m16_native.h\n`);
run('make',['--no-print-directory','-f',makefile,`OUT_DIR=${objects}`,'quietlock']);
const spy=path.join(root,'compiler.mjs');
fs.writeFileSync(spy,`#!/usr/bin/env node
import fs from 'node:fs'; import net from 'node:net'; import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
const args=process.argv.slice(2);
if(!args.includes('-c')) {const r=spawnSync('cc',args,{stdio:'inherit'});process.exit(r.status??1);}
const source=args[args.indexOf('-c')+1],target=args[args.indexOf('-o')+1];
const include=args.find(x=>x.startsWith('-I')&&x.endsWith('/m16'));
if(!include) throw Error('missing actual physics include directory');
const header=include.slice(2)+'/m16_native.h';
const r=spawnSync('git',['-C',include.slice(2), 'rev-parse','HEAD'],{encoding:'utf8'});
const record={kind:'SYNTHETIC_NON_OBJECT',source,header,head:r.stdout.trim(),header_sha256:crypto.createHash('sha256').update(fs.readFileSync(header)).digest('hex'),stamp:fs.existsSync(process.env.L532_STAMP)?fs.readFileSync(process.env.L532_STAMP,'utf8'):null};
if(process.env.L532_BARRIER) await new Promise((resolve,reject)=>{const s=net.createConnection(process.env.L532_BARRIER,()=>s.write(JSON.stringify(record)+'\\n'));s.on('data',()=>{s.end();resolve();});s.on('error',reject);});
fs.appendFileSync(process.env.L532_LOG,JSON.stringify(record)+'\\n');
fs.writeFileSync(target,JSON.stringify(record));
fs.writeFileSync(target.replace(/\\.o$/,'.d'),target+': '+source+' '+header+'\\n');
`); fs.chmodSync(spy,0o755);
const log=path.join(root,'compiles.jsonl');fs.writeFileSync(log,'');
function build(p,extra={},goal=target) {
    const child=spawn('make',['--no-print-directory','-f',makefile,`OUT_DIR=${objects}`,`PHYSICS_DIR=${p}`,`CC=${spy}`,goal],
        {env:{...process.env,L532_STAMP:stamp,L532_LOG:log,...extra}});
    let output=''; child.stdout.on('data',b=>output+=b); child.stderr.on('data',b=>output+=b);
    return new Promise((resolve,reject)=>child.on('close',code=>{
        fs.appendFileSync(path.join(root,'make.log'),output);
        if(code) reject(Error(`make exit=${code}: ${output}`));else resolve();
    }).on('error',reject));
}
const count=()=>fs.readFileSync(log,'utf8').trim().split('\n').filter(Boolean).length;
function check(ok,name,details={}) {
    console.log(JSON.stringify({verdict:ok?'PASS':'FAIL',name,...details}));
    if(!ok) throw Error(name);
}
function current(p) {
    const marker=JSON.parse(fs.readFileSync(target));
    return marker.header===path.join(p,'m16/m16_native.h') && marker.head===git(p,'rev-parse','HEAD').trim()
        && marker.header_sha256===sha(fs.readFileSync(path.join(p,'m16/m16_native.h')));
}
let server;
async function barrier() {
    // Short /tmp socket name avoids Unix path-length limits. Only this process owns it.
    const sock=path.join('/tmp',`L532-${process.pid}-${crypto.randomBytes(4).toString('hex')}.sock`);
    let arrivedResolve; const arrived=new Promise(r=>arrivedResolve=r);
    let connection; server=net.createServer(s=>{connection=s;let data='';s.on('data',b=>{data+=b;if(data.includes('\n'))arrivedResolve(JSON.parse(data));});});
    await new Promise(r=>server.listen(sock,r));
    return {sock,arrived,release:()=>connection.write('release\n'),close:()=>server.close()};
}
async function lockWait(pending) {
    let done=false;pending.then(()=>done=true);
    const lock=objects+'.omega-build.lock';
    let observed=false;
    while(!done) {
        if(fs.existsSync(lock)) {
            const inode=fs.statSync(lock).ino;
            observed=fs.readFileSync('/proc/locks','utf8').split('\n').some(l=>l.includes('->') && l.includes(':'+inode+' '));
            if(observed)break;
        }
        await delay(10);
    }
    check(observed,'actual_kernel_lock_waiter');
}
const watchdog=setTimeout(()=>{console.error('FAIL barrier deadline (no timing-based verdict)');process.exit(2);},30000);
try {
    await build(a);check(count()===1 && current(a),'initial_real_rule_header_consumer');
    await build(a);check(count()===1,'valid_cache_hit');
    if(mode==='serial') {
        await build(b);check(count()===2 && current(b),'directory_switch_older_header');
        commit(b);await build(b);check(count()===3 && current(b),'revision_switch_unchanged_bytes');
        const header=path.join(b,'m16/m16_native.h');fs.appendFileSync(header,'/* dirty header */\n');old(header);
        await build(b);check(count()===4 && current(b),'dirty_header_older_mtime');
        await build(b);check(count()===4,'unchanged_dirty_cache_hit');
        const input=path.join(b,'m16/m16_native.c');fs.appendFileSync(input,'/* dirty driver input */\n');old(input);
        await build(b);check(count()===5,'dirty_input_invalidates_header_consumer');
        fs.symlinkSync(b,path.join(root,'alias'));await build(path.join(root,'alias'));check(count()===5,'resolved_alias_cache_hit');
        await build(a);check(count()===6 && current(a),'switch_back');
    } else if(mode==='concurrent' || mode==='same') {
        // The original B-completes-before-A schedule is forbidden by serialization.
        // A captures and pauses; B starts and reaches an observed kernel wait;
        // release A, then B completes. Timeouts are failures, never pass evidence.
        commit(a);const gate=await barrier();
        const pending=build(a,{L532_BARRIER:gate.sock});
        const captured=await gate.arrived;
        const waiting=build(b);
        await lockWait(waiting);
        check(count()===1 && !current(b),'b_waiting_before_dependency_read');
        gate.release();await pending;
        await waiting;check(current(b),'overlapping_builder_b_completed');
        const bStamp=fs.existsSync(stamp)?fs.readFileSync(stamp,'utf8'):null;
        gate.close();
        const after=JSON.parse(fs.readFileSync(target));
        const beforeCount=count();await build(b);
        const state={captured_a:captured,b_stamp:bStamp,published:after,final_stamp:fs.existsSync(stamp)?fs.readFileSync(stamp,'utf8'):null,cache_hit:count()===beforeCount};
        fs.writeFileSync(path.join(root,'concurrent-state.json'),JSON.stringify(state,null,2));
        check(current(b),'no_mismatched_output_accepted_as_current',state);
        check(state.cache_hit && state.published.stamp===state.final_stamp,'artifact_header_source_stamp_consistency');
        check(count()===(mode==='same'?2:3),'same_or_distinct_identity_compile_count');
    } else {
        // Two forced refreshes of the same identity must preserve the cached object
        // and publish exactly the same complete stamp, with no .tmp debris.
        const before=fs.readFileSync(stamp,'utf8');
        commit(a);
        const gate=await barrier(), bin=path.join(root,'bin');fs.mkdirSync(bin);
        const mv=path.join(bin,'mv');
        fs.writeFileSync(mv,`#!/usr/bin/env node
import fs from 'node:fs'; import net from 'node:net';import {spawnSync} from 'node:child_process';
const args=process.argv.slice(2);
if(process.env.L532_REFRESH_BARRIER)await new Promise((resolve,reject)=>{const s=net.createConnection(process.env.L532_REFRESH_BARRIER,()=>s.write(JSON.stringify({candidate:fs.readFileSync(args[args.length-2],'utf8'),published:fs.readFileSync(args[args.length-1],'utf8')})+'\\n'));s.on('data',()=>{s.end();resolve();});s.on('error',reject);});
const r=spawnSync('/bin/mv',args,{stdio:'inherit'});process.exit(r.status??1);
`);fs.chmodSync(mv,0o755);
        const pending=build(a,{PATH:bin+':'+process.env.PATH,L532_REFRESH_BARRIER:gate.sock},stamp);
        const captured=await gate.arrived;
        check(captured.published===before,'refresh_candidate_private_before_rename');
        const waiting=build(b,{},stamp);
        await lockWait(waiting);
        check(fs.readFileSync(stamp,'utf8')===before,'refresh_b_waits_before_publication');
        gate.release();await pending;await waiting;
        const bStamp=fs.readFileSync(stamp,'utf8');
        check(bStamp.includes('dir='+b+'\n'),'overlapping_refresh_b_published');
        gate.close();
        const after=fs.readFileSync(stamp,'utf8');
        fs.writeFileSync(path.join(root,'refresh-state.json'),JSON.stringify({before,captured,bStamp,after},null,2));
        check(after===bStamp && /^dir=.+\ncommit=[0-9a-f]{40}\ndiff_sha256=[0-9a-f]{64}\n$/.test(after),'complete_unchanged_stamp');
        await build(a);check(count()===2 && current(a),'refresh_rebuilds_matching_output');
        await Promise.all([build(a,{},stamp),build(a,{},stamp)]);
        await build(a);check(count()===2 && current(a),'concurrent_refresh_preserves_cache');
        check(!fs.readdirSync(objects).some(x=>x.startsWith('physics.stamp.tmp.')),'no_temporary_stamp_debris');
    }
    console.log('PHYSICS_HEADER_IDENTITY_PASS (structural host only; driver/chip NOT_RUN)');
} catch(e) {console.error(`FAIL ${e.message}`);process.exitCode=1;}
finally {clearTimeout(watchdog);if(server)server.close();}
