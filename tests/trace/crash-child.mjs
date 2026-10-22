// Invoked only by review-fixes.test.mjs against its own private fixtures.
import fs from 'node:fs';
import {Archive,BoundedWriter} from '../../tools/trace/archive.mjs';
import {fixture} from './helpers.mjs';
const [mode,root]=process.argv.slice(2);
const pause=message=>{process.send(message);setInterval(()=>{},1000);};
if(mode==='startup'){
 const w=new BoundedWriter({root,enabled:true});const accepted=w.submit(fixture());
 w.worker.on('message',m=>{if(m.test_cut)pause({cut:m.test_cut,accepted,status:w.status()});});
}else if(mode==='queued'){
 // Block actual worker fsync after its lock is durable, before enqueue processing.
 const w=new BoundedWriter({root,enabled:true});const accepted=w.submit(fixture());
 w.worker.on('message',m=>{if(m.test_cut)pause({cut:m.test_cut,accepted,status:w.status()});});
}else if(mode==='purge'){
 const a=new Archive(root);a.append([fixture()]);
 const sync=fs.fsyncSync;
 fs.fsyncSync=function(fd){sync(fd);const target=fs.readlinkSync(`/proc/self/fd/${fd}`);
  if(target.endsWith('/index.jsonl')&&fs.readFileSync(target,'utf8').includes('"op":"remove"')){
   process.send({cut:'after_tombstone_fsync'});Atomics.wait(new Int32Array(new SharedArrayBuffer(4)),0,0);
  }
 };
 a.purge();
}else if(mode==='clean'||mode==='startup-failure'){
 const w=new BoundedWriter({root,enabled:true});w.submit(fixture());const status=await w.close();process.send({cut:'clean',status});process.disconnect();
}else throw Error('unknown fixture mode');
