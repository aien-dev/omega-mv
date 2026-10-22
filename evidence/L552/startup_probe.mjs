// L552 independent actual fsync failure, owned synthetic archives only.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {pathToFileURL} from 'node:url';
const [source,out,mode]=process.argv.slice(2);fs.mkdirSync(out,{recursive:false});
const {Archive,BoundedWriter,readArchive,recover}=await import(pathToFileURL(path.join(source,'tools/trace/archive.mjs')));
const {fixture}=await import(pathToFileURL(path.join(source,'tests/trace/helpers.mjs')));
const snap=root=>fs.readdirSync(root).sort().map(n=>[n,fs.readFileSync(path.join(root,n)).toString('hex')]);const rows=[];
for(const stage of [1,2]){
 const root=path.join(out,'fsync-'+stage),a=new Archive(root);a.append([fixture(100)]);a.close();const before=snap(root);
 const orig=fs.fsyncSync;let calls=0,injected=false,w;
 try{fs.fsyncSync=fd=>{if(++calls===stage){injected=true;throw Object.assign(new Error('L552 reservation fsync'),{code:'EIO'});}return orig(fd);};w=new BoundedWriter({root,enabled:true});}finally{fs.fsyncSync=orig;}
 const immediate=w.status(),accepted=[w.submit(fixture(1)),w.submit(fixture(2))],post=w.status(),archiveBeforeClose=readArchive(root);
 const closed=await w.close();const after=readArchive(root);
 rows.push({case:'fsync-'+stage,injected,immediate,accepted,post,closed,archiveBeforeClose:archiveBeforeClose.integrity,archiveAfter:after.integrity,events:after.events.length,bytes_unchanged:JSON.stringify(before)===JSON.stringify(snap(root))});
 assert.ok(injected);assert.deepEqual(snap(root),before);assert.equal(after.events.length,1);assert.equal(closed.durable,false);
 if(mode==='fixed'){assert.ok(immediate.errors.length);assert.deepEqual(accepted,[false,false]);assert.equal(post.submitted,0);assert.equal(post.accepted_pending,0);}
 else {assert.deepEqual(accepted,[true,true]);assert.equal(post.submitted,2);assert.equal(archiveBeforeClose.integrity,'PASS');}
 const dest=path.join(out,'recovered-'+stage);recover(root,dest);assert.equal(readArchive(dest).integrity,'PASS');assert.equal(readArchive(dest).events.length,1);
}
if(mode==='fixed'){
 const root=path.join(out,'busy'),a=new Archive(root),before=snap(root),w=new BoundedWriter({root,enabled:true});
 assert.equal(w.submit(fixture()),false);assert.equal(w.status().submitted,0);const closed=await w.close();assert.equal(closed.durable,false);assert.deepEqual(snap(root),before);assert.equal(readArchive(root).integrity,'INCONCLUSIVE');a.close();assert.equal(readArchive(root).integrity,'PASS');rows.push({case:'busy-session',submitted:closed.submitted,errors:closed.errors,foreign_marker_preserved:true});
 const missing=path.join(out,'missing','archive'),m=new BoundedWriter({root:missing,enabled:true});assert.equal(m.submit(fixture()),false);const closedM=await m.close();assert.equal(closedM.submitted,0);assert.equal(closedM.accepted_pending,0);assert.equal(fs.existsSync(path.join(out,'missing')),false);rows.push({case:'missing-parent',status:closedM});
 const good=path.join(out,'healthy'),v=new BoundedWriter({root:good,enabled:true});assert.equal(readArchive(good).integrity,'INCONCLUSIVE');assert.equal(v.submit(fixture(101)),true);const healthy=await v.close();assert.equal(healthy.durable,true);assert.equal(healthy.persisted,1);assert.equal(readArchive(good).integrity,'PASS');rows.push({case:'healthy',status:healthy});
}
fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(rows,null,2)+'\n');console.log(JSON.stringify({mode,cases:rows.length,rows}));
