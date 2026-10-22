// SYNTHETIC integration. Run inside network-deny and strace, not a model trace.
import fs from 'node:fs';import path from 'node:path';import os from 'node:os';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
import {BoundedWriter,readArchive,Archive} from '../../tools/trace/archive.mjs';import {canonical,hash,metric} from '../../tools/trace/schema.mjs';import {fixture,id} from './helpers.mjs';import {main} from '../../tools/trace/trace.mjs';
let checks=0;const check=new Proxy(assert,{get(target,key){const value=target[key];return typeof value==='function'?(...args)=>{checks++;return value(...args);}:value;}});
const p=fs.mkdtempSync(path.join(os.tmpdir(),'omega-trace-offline-')),root=path.join(p,'archive');
try{
 const w=new BoundedWriter({enabled:true,root,capacity:3});for(let n=1;n<=50;n++)w.submit(fixture(n));const status=await w.close();check.deepEqual(status.errors,[]);check.equal(readArchive(root).integrity,'PASS');
 const before=Object.fromEntries(fs.readdirSync(root).map(n=>[n,hash(fs.readFileSync(path.join(root,n)))]));
 const preview=await main(['--root',root,'share-preview',id(1),'--fields','duration']);await main(['--root',root,'share-package',id(1),'--fields','duration','--output',path.join(p,'bundle.json')]);check.equal(preview,fs.readFileSync(path.join(p,'bundle.json'),'utf8'));
 for(const command of ['list','show','steps','events','verify','stats'])JSON.parse(await main(['--root',root,command,...(['list','stats'].includes(command)?[]:[id(1)])]));
 await main(['--root',root,'viewer',id(1),'--output',path.join(p,'viewer.html')]);check.ok(fs.readFileSync(path.join(p,'viewer.html'),'utf8').includes('connect-src'));
 check.deepEqual(before,Object.fromEntries(fs.readdirSync(root).map(n=>[n,hash(fs.readFileSync(path.join(root,n)))])));
 const staging=path.join(p,'input');fs.mkdirSync(staging,{mode:448});fs.copyFileSync('evidence/L489/synthetic-searchtrace.txt',path.join(staging,'corpus.txt'));fs.chmodSync(path.join(staging,'corpus.txt'),384);fs.writeFileSync(path.join(staging,'context.json'),canonical(fixture()),{mode:384});
 const tool=path.resolve(process.env.OMEGA_TRACE_BUILD_DIR??'build/L489','searchtrace/searchtrace'),pin=hash(fs.readFileSync(tool)),imported=path.join(p,'imported');
 const result=JSON.parse(await main(['--root',imported,'import-searchtrace','--input-root',staging,'--input','corpus.txt','--context','context.json','--verifier',tool,'--verifier-hash',pin]));check.ok(result.events>0);check.equal(readArchive(imported).integrity,'PASS');
 // Actual subprocess CLI, disconnected environment and no tracing enabled.
 const cli=spawnSync(process.execPath,['tools/trace/trace.mjs','--root',root,'stats'],{stdio:['ignore','ignore','inherit']});check.equal(cli.status,0);
 console.log(canonical({schema:'omega.trace.offline-test.v1',qualification:'SYNTHETIC_HOST_ONLY',network_policy:process.env.OMEGA_TRACE_NETWORK_DENIED==='1'?'seccomp_socket_operations_denied':'UNRESTRICTED_DIAGNOSTIC_ONLY',accepted:readArchive(root).events.filter(e=>e.category==='step').length,loss_summaries:readArchive(root).events.filter(e=>e.category==='loss').length,dropped:status.dropped,imported_events:result.events,checks,source_unchanged:true,preview_exact:true,listener:false,hardware:'NOT_RUN',performance:'NOT_RUN'}));
}finally{fs.rmSync(p,{recursive:true,force:true});}
