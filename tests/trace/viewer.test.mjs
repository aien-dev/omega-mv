import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';
import {Archive,readArchive} from '../../tools/trace/archive.mjs';import {query,compare} from '../../tools/trace/query.mjs';import {renderViewer,viewState} from '../../tools/trace/viewer.mjs';import {metric} from '../../tools/trace/schema.mjs';import {fixture,temp,id} from './helpers.mjs';
function data(t){const root=path.join(temp(t),'a'),a=new Archive(root);a.append([fixture(1,{metrics:[metric('loss',0.5,'ratio','observed'),metric('rate',2,'per_second','observed')]}),fixture(2),fixture(3,{category:'loss',metrics:[metric('dropped',3,'events','observed')]}),fixture(4,{run:id(9),contract:{machine:id(8),build:id(4),config:id(5),precision:'bf16'}})]);a.close();const archive=readArchive(root);return {root,q:query(archive,{run:id(1)}),comparison:compare(archive,id(1),id(9))};}
test('self-contained offline artifact, CSP, no listener/remote scripts, integrity/proof distinction',t=>{
 const {q,comparison}=data(t),a=renderViewer(q,{comparison});assert.equal(a,renderViewer(q,{comparison}));assert.match(a,/default-src 'none'/);assert.match(a,/connect-src 'none'/);assert.ok(!/<script[^>]+src=|(?:src|href)=["']https?:|\bfetch\(|WebSocket|XMLHttpRequest|<form|<iframe/i.test(a));assert.match(a,/NOT_VERIFIED/);assert.match(a,/synthetic/);assert.match(a,/precision/);assert.match(a,/dropped/);
});
test('step/source/type/time selection and missing/loss/compare labels use typed observations',t=>{
 const {q}=data(t),s=viewState(q.events,{step:'2',source:'train',from:'2',to:'2'});assert.equal(s.events.length,1);assert.equal(s.energy,'unavailable');assert.equal(viewState(q.events,{}).dropped,3);assert.throws(()=>viewState(q.events,{from:'3',to:'1'}));assert.throws(()=>viewState(q.events,{from:'-1'}));assert.equal(viewState(q.events,{step:'1'}).charts[0].name,'loss');
});
class Element{constructor(tag){this.tagName=tag;this.children=[];this.textContent='';this.value='';this.listeners={};this.attributes={};}append(...xs){this.children.push(...xs);}replaceChildren(...xs){this.children=[...xs];}setAttribute(k,v){this.attributes[k]=String(v);}addEventListener(k,fn){this.listeners[k]=fn;}}
test('actual bundled client interactions: filters, step selection, time, comparison, reset; immutable data',t=>{
 const {q,comparison}=data(t),html=renderViewer(q,{comparison}),payload=html.match(/<script id="trace-data" type="application\/json">([\s\S]*?)<\/script>/)[1],script=html.match(/<script id="trace-client">([\s\S]*?)<\/script>/)[1];
 const elements=new Map(),document={getElementById(id){if(!elements.has(id))elements.set(id,new Element('div'));return elements.get(id);},createElement:tag=>new Element(tag),createElementNS:(_,tag)=>new Element(tag)};document.getElementById('trace-data').textContent=payload;
 vm.runInNewContext(script,{document,console,JSON,Number,BigInt,Set,Map,Math,Error});assert.match(document.getElementById('notice').textContent,/NOT_VERIFIED/);assert.match(document.getElementById('comparison').textContent,/qualified/);
 const snapshot=()=>JSON.stringify([...elements]);assert.ok(snapshot().includes('synthetic'));
 const step=document.getElementById('step');step.value='2';step.listeners.change();assert.equal(document.getElementById('events').children.length,1);
 const from=document.getElementById('from');from.value='9';from.listeners.input();assert.match(document.getElementById('warning').textContent,/no events|range/);
 document.getElementById('reset').listeners.click();assert.equal(document.getElementById('events').children.length,3);assert.equal(document.getElementById('trace-data').textContent,payload);
});
test('unknown dropped count remains unknown in viewer, never zero',()=>{
 const e=fixture(1,{category:'loss',metrics:[metric('dropped',null,'events','unknown','not_recorded')]});assert.equal(viewState([e],{}).dropped,'unknown');
});
