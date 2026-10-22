import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {Archive,BoundedWriter,readArchive} from '../../tools/trace/archive.mjs';
import {fixture,temp} from './helpers.mjs';

test('parent: failed synchronous reservation refuses immediate enqueue', async t=>{
 const root=path.join(temp(t),'a');new Archive(root).close();
 const original=fs.fsyncSync;let injected=false,w;
 try{
  fs.fsyncSync=(fd)=>{if(!injected){injected=true;throw Object.assign(new Error('injected reservation fsync failure'),{code:'EIO'});}return original(fd);};
  w=new BoundedWriter({root,enabled:true});
 }finally{fs.fsyncSync=original;}
 t.after(async()=>{await w.close();});
 assert.equal(injected,true);
 const accepted=w.submit(fixture());
 console.log(JSON.stringify({accepted,status:w.status(),archive:readArchive(root).integrity}));
 assert.equal(accepted,false,'failed reservation must refuse before worker error delivery');
 assert.equal(w.status().submitted,0);
 assert.equal(w.status().accepted_pending,0);
 assert.ok(w.status().errors.length>0);
 const status=await w.close();assert.equal(status.durable,false);
});
