// Deterministic SYNTHETIC review artifacts. Run explicitly, never on actual model traces.
import fs from 'node:fs';import path from 'node:path';import os from 'node:os';import assert from 'node:assert/strict';import {Archive,readArchive} from '../../tools/trace/archive.mjs';import {fixture,id} from './helpers.mjs';import {metric} from '../../tools/trace/schema.mjs';import {query,compare,exportTable} from '../../tools/trace/query.mjs';import {contribution} from '../../tools/trace/share.mjs';import {renderViewer} from '../../tools/trace/viewer.mjs';
function write(name,bytes){if(fs.existsSync(name)){assert.equal(fs.readFileSync(name,'utf8'),bytes,'frozen artifact differs: '+name);return;}fs.writeFileSync(name,bytes,{flag:'wx'});}
const temp=fs.mkdtempSync(path.join(os.tmpdir(),'omega-trace-artifacts-'));try{
 const root=path.join(temp,'archive'),a=new Archive(root,{clock:()=>0});a.append([fixture(1),fixture(2,{metrics:[metric('duration',4,'ns','observed'),metric('energy',null,'J','unknown','not_recorded')]}),fixture(3,{category:'loss',metrics:[metric('dropped',7,'events','observed')]})]);a.close();
 const archive=readArchive(root),q=query(archive,{run:id(1)}),bundle=contribution(q,{fields:['duration'],preset:'summary'});
 write('tests/trace/golden-contribution.json',bundle.bytes);write('evidence/L489/synthetic-package.json',bundle.bytes);write('evidence/L489/synthetic-query-final.json',exportTable(q,'json'));write('evidence/L489/synthetic-query-final.csv',exportTable(q,'csv'));write('evidence/L489/synthetic-viewer-final.html',renderViewer(q));
 console.log(JSON.stringify({schema:'omega.trace.artifacts.v1',qualification:'SYNTHETIC_HOST_ONLY',records:q.events.length,package_digest:bundle.digest}));
}finally{fs.rmSync(temp,{recursive:true,force:true});}
