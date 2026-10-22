import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';
import {hash,metric} from '../../tools/trace/schema.mjs';import {searchtrace,argus,dual} from '../../tools/trace/observers.mjs';import {fixture,id} from './helpers.mjs';
const base=fixture();
const bin=path.resolve(process.env.OMEGA_TRACE_BUILD_DIR??'build/L489','searchtrace/searchtrace'),corpusPath='evidence/L489/synthetic-searchtrace.txt';
test('existing hook corpus maps causal candidate/realization pairs without copying task text',()=>{
 const bytes=fs.readFileSync(corpusPath),out=searchtrace(bytes,base,{verifier:bin,verifierHash:hash(fs.readFileSync(bin))});
 assert.ok(out.length>0);assert.equal(out[0].source,'searchtrace');assert.ok(out.some(e=>e.parent));assert.ok(out.some(e=>e.metrics.some(m=>m.name==='cost'&&m.provenance==='estimated')));
 assert.ok(out.every(e=>e.qualification==='synthetic'));assert.ok(!JSON.stringify(out).includes('pname'));assert.deepEqual(searchtrace(bytes,base,{verifier:bin,verifierHash:hash(fs.readFileSync(bin))}),out);
 assert.throws(()=>searchtrace(bytes.subarray(0,bytes.length-1),base,{verifier:bin,verifierHash:hash(fs.readFileSync(bin))}));assert.throws(()=>searchtrace(bytes,base,{verifier:bin,verifierHash:id(0)}));
});
function argusRecord(kind=70,seq=1n){const b=Buffer.alloc(128);b[0]=1;b[1]=kind===80?1:2;b.writeUInt16LE(kind,2);b[5]=1;b.writeUInt16LE(1|(3<<2)|(kind===80?2:0),6);b.writeBigUInt64LE(seq,8);b.writeBigUInt64LE(7n,16);b.writeUInt32LE(0xffffffff,32);Buffer.from(id(3),'hex').copy(b,64);Buffer.from(id(8),'hex').copy(b,96);return b;}
test('ARGUS existing ABI keeps u64 stream/tick, original refs, sticky critical loss and refuses malformed',()=>{
 const b=argusRecord(),loss=argusRecord(80,2n);loss.writeUInt32LE(1,36);loss.writeBigUInt64LE(4n,56);const source=Buffer.concat([b,loss]);const out=argus(source,base);assert.equal(out[0].producer,3);assert.equal(out[0].time.tick,'7');assert.ok(out[0].receipts.includes(id(8)));assert.equal(out[0].parent,null);assert.equal(out[1].metrics.find(m=>m.name==='critical_loss').value,4);
 for(const bad of [source.subarray(1),Buffer.from(source)]){if(bad.length===source.length)bad[0]=2;assert.throws(()=>argus(bad,base));}
 assert.throws(()=>argus(Buffer.concat([b,b]),base));const u=argusRecord(70,0xffffffffffffffffn);assert.throws(()=>argus(u,base));
});
export function dualRecord(){const b=Buffer.alloc(2+64+8+4+32+12+44+4);let p=0;b[p++]=5;b[p++]=1;Buffer.from(id(11),'hex').copy(b,p);p+=32;Buffer.from(id(12),'hex').copy(b,p);p+=32;b.writeBigUInt64LE(1n,p);p+=8;b.writeUInt32LE(1,p);p+=4;Buffer.from(id(13),'hex').copy(b,p);p+=32;p+=8;b.writeUInt32LE(1,p);p+=4;b.writeUInt32LE(1,p);p+=4;b.writeUInt32LE(9,p);p+=4;b.writeDoubleLE(10,p);p+=8;b.writeDoubleLE(2,p);p+=8;b.writeUInt32LE(1,p);p+=4;b.writeDoubleLE(12,p);p+=8;b.writeDoubleLE(1,p);return b;}
test('DUAL canonical recommendation remains advisory, uncalibrated and measured distinct',()=>{
 const b=dualRecord(),e=dual(b,base)[0];assert.equal(e.status,'uncalibrated');assert.equal(e.semantic,id(11));assert.equal(e.metrics.find(m=>m.name==='predicted').provenance,'estimated');assert.equal(e.metrics.find(m=>m.name==='measured').value,12);
 const bad=Buffer.from(b);bad.writeUInt32LE(1,bad.length-4);assert.throws(()=>dual(bad,base));assert.throws(()=>dual(b.subarray(0,b.length-1),base));assert.throws(()=>dual(b,base,{state:'fresh'}));
 const stale=dual(b,base,{state:'stale'})[0];assert.equal(stale.status,'stale');
});
test('DUAL preserves valid signed predictions and zero resource IDs from native contract',()=>{
 const b=dualRecord(),start=2+64+8+4+32+12;b.writeUInt32LE(0,start);b.writeDoubleLE(-10,start+8);b.writeDoubleLE(-8,start+28);b.writeDoubleLE(1,start+36);
 const e=dual(b,base)[0];assert.equal(e.metrics.find(m=>m.name==='predicted').value,-10);assert.equal(e.metrics.find(m=>m.name==='measured').value,-8);
});
test('realization code size is a distinct typed metric, not memory footprint',()=>{
 const out=searchtrace(fs.readFileSync(corpusPath),base,{verifier:bin,verifierHash:hash(fs.readFileSync(bin))});const r=out.find(e=>e.category==='realization');assert.ok(r.metrics.some(m=>m.name==='code_bytes'));assert.ok(!r.metrics.some(m=>m.name==='memory'));
});
