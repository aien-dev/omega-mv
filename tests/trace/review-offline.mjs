// SYNTHETIC follow-on privacy/recovery workflow. No child-process IPC fixture.
import fs from 'node:fs';import os from 'node:os';import path from 'node:path';import assert from 'node:assert/strict';
import {Archive,readArchive,recover} from '../../tools/trace/archive.mjs';
import {canonical,metric} from '../../tools/trace/schema.mjs';import {query} from '../../tools/trace/query.mjs';import {contribution} from '../../tools/trace/share.mjs';import {main} from '../../tools/trace/trace.mjs';import {fixture,id} from './helpers.mjs';
let checks=0;const check=new Proxy(assert,{get(target,key){const v=target[key];return typeof v==='function'?(...args)=>{checks++;return v(...args);}:v;}});
const snapshot=root=>fs.readdirSync(root).sort().map(n=>[n,fs.readFileSync(path.join(root,n)).toString('hex')]);
const p=fs.mkdtempSync(path.join(os.tmpdir(),'L516-offline-'));
try{
 const root=path.join(p,'archive'),a=new Archive(root);a.append([fixture(123456,{producer:4321,sequence:'987654321',metrics:[metric('loss',0.5,'ratio','observed')]})]);a.close();
 const before=snapshot(root),preview=await main(['--root',root,'share-preview',id(1),'--fields','loss','--preset','spans']);
 await main(['--root',root,'share-package',id(1),'--fields','loss','--preset','spans','--output',path.join(p,'spans.json')]);
 check.equal(fs.readFileSync(path.join(p,'spans.json'),'utf8'),preview);const b=JSON.parse(preview);check.ok(b.disclosures.privacy_surface.mandatory_span_fields.includes('producer'));check.ok(b.disclosures.privacy_surface.mandatory_span_fields.includes('time.tick'));check.equal(b.spans[0].producer,4321);check.deepEqual(snapshot(root),before);
 fs.writeFileSync(path.join(root,'.writer.lock'),'synthetic-dead-session',{mode:0o600});const crashed=snapshot(root),r=readArchive(root);check.equal(r.integrity,'INCONCLUSIVE');check.throws(()=>contribution(query(r,{run:id(1)}),{fields:['loss']}));
 recover(root,path.join(p,'recovered'));check.equal(readArchive(path.join(p,'recovered')).integrity,'INCONCLUSIVE');check.equal(JSON.parse(fs.readFileSync(path.join(p,'recovered/recovery.json'))).loss_extent,'unknown');check.deepEqual(snapshot(root),crashed);
 const purged=path.join(p,'purged'),c=new Archive(purged);c.append([fixture()]);const entry=readArchive(purged).segments[0];c.index({op:'remove',name:entry.name,reason:'purge',digest:entry.digest,losses:[]});c.close();
 const residual=snapshot(purged),q=readArchive(purged);check.equal(q.integrity,'INCONCLUSIVE');check.match(q.problems.join(' '),/tombstoned.*residual/);check.equal(q.events.length,0);check.deepEqual(snapshot(purged),residual);
 console.log(canonical({schema:'omega.trace.review-offline.v1',qualification:'SYNTHETIC_HOST_ONLY',checks,preview_exact:true,source_unchanged:true,unclean_loss_extent:'unknown',residual_purge:'INCONCLUSIVE',network_policy:process.env.OMEGA_TRACE_NETWORK_DENIED==='1'?'seccomp_socket_operations_denied':'UNRESTRICTED_DIAGNOSTIC_ONLY'}));
}finally{fs.rmSync(p,{recursive:true,force:true});}
