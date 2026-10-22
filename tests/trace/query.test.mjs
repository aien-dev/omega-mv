import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {Archive,readArchive} from '../../tools/trace/archive.mjs';
import {metric,canonical} from '../../tools/trace/schema.mjs';
import {query,compare,exportTable} from '../../tools/trace/query.mjs';
import {fixture,temp,id} from './helpers.mjs';
function data(t){const root=path.join(temp(t),'a'),a=new Archive(root),parent=fixture(1,{time:{domain:'logical',tick:'5',wall_ns:null}}),child=fixture(2,{producer:2,parent:parent.event_id,time:{domain:'logical',tick:'5',wall_ns:null},metrics:[metric('energy',null,'J','unknown','not_recorded')]});a.append([child,parent,fixture(3,{run:id(10),contract:{...parent.contract,precision:'bf16'}})]);a.close();return {root,parent,child};}
test('multi-producer collision ordering, exact links, typed totals and unavailable energy',t=>{
 const {root,parent,child}=data(t),r=query(readArchive(root),{run:id(1)});assert.deepEqual(r.events.map(e=>e.event_id),[parent.event_id,child.event_id]);assert.deepEqual(r.timeline.map(e=>e.parent),[null,parent.event_id]);assert.equal(r.summary.events,2);assert.equal(r.summary.energy.available,false);assert.equal(r.evidence,'NOT_VERIFIED');assert.equal(r.summary.metrics[0].name,'duration');
 assert.equal(canonical(r),canonical(query(readArchive(root),{run:id(1)})));
});
test('all filters, inclusive time bounds, step selection and retained causal references',t=>{
 const {root,child}=data(t),a=readArchive(root),r=query(a,{run:id(1),session:id(2),source:'train',type:'step',step:2,status:'ok',from:'5',to:'5'});assert.equal(r.events.length,1);assert.equal(r.events[0].event_id,child.event_id);assert.equal(r.timeline[0].parent_visible,false);assert.equal(r.timeline[0].parent_known,true);
 assert.throws(()=>query(a,{from:'6',to:'5'}));assert.throws(()=>query(a,{unknown:'x'}));assert.throws(()=>query(a,{source:'evil'}));
});
test('comparison qualifies mismatched contracts; export carries source digests and query',t=>{
 const {root}=data(t),a=readArchive(root),r=compare(a,id(1),id(10));assert.equal(r.directly_comparable,false);assert.ok(r.differences.includes('precision'));
 const q=query(a,{run:id(1)}),out=exportTable(q,'json');assert.deepEqual(JSON.parse(out).source_segments,q.source_segments);assert.equal(JSON.parse(out).query.run,id(1));
 const csv=exportTable(q,'csv');assert.match(csv,/source_segments/);assert.match(csv,/duration/);assert.match(csv,/observed/);
});
test('missing/corrupt archive reports inconclusive rather than successful empty result',t=>{
 const {root}=data(t),n=fs.readdirSync(root).find(n=>n.endsWith('.seg'));fs.unlinkSync(path.join(root,n));const q=query(readArchive(root),{run:id(1)});assert.equal(q.integrity,'INCONCLUSIVE');assert.ok(q.problems.length);
});
test('CLI reads while tracing disabled; all commands return deterministic versioned results',t=>{
 const {root}=data(t),before=fs.readdirSync(root).map(n=>fs.readFileSync(path.join(root,n)).toString('hex'));
 for(const args of [['list'],['show',id(1)],['steps',id(1)],['events',id(1),'--range','5:5'],['compare',id(1),id(10)],['verify',id(1)],['stats']]){
 const p=spawnSync(process.execPath,['tools/trace/trace.mjs','--root',root,...args],{encoding:'utf8',env:{PATH:process.env.PATH}});assert.equal(p.status,0,p.stderr);assert.match(JSON.parse(p.stdout).schema,/omega.trace/);
 }
 assert.deepEqual(before,fs.readdirSync(root).map(n=>fs.readFileSync(path.join(root,n)).toString('hex')));
});
test('unknown run and mixed clock range cannot report conclusive success',t=>{
 const {root}=data(t),a=readArchive(root);assert.equal(query(a,{run:id(999)}).integrity,'INCONCLUSIVE');
 const mixed={...a,events:[...a.events,fixture(4,{time:{domain:'monotonic',tick:'5',wall_ns:null}})]};assert.equal(query(mixed,{run:id(1),from:'1',to:'8'}).integrity,'INCONCLUSIVE');
});
test('cross-session links within a run remain valid; cross-run parents are qualified',()=>{
 const parent=fixture(1),child=fixture(2,{parent:parent.event_id,session:id(999)}),archive={events:[parent,child],segments:[],retention:[],problems:[],integrity:'PASS'};assert.equal(query(archive,{run:id(1)}).integrity,'PASS');const other={...archive,events:[parent,fixture(2,{parent:parent.event_id,run:id(999)})]};assert.equal(query(other,{run:id(999)}).integrity,'INCONCLUSIVE');
});
test('CLI refuses misleading arity/irrelevant filters and requires run for steps',async t=>{
 const {main}=await import('../../tools/trace/trace.mjs');const root=path.join(temp(t),'isolated');for(const args of [['purge','run-only'],['steps'],['stats','--output','/tmp/ignored'],['verify',id(1),'--run',id(2)]])await assert.rejects(()=>main(['--root',root,...args]));
});
