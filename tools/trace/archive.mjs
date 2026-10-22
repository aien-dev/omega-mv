import fs from 'node:fs';
import path from 'node:path';
import {Worker} from 'node:worker_threads';
import {OwnedRoot} from './paths.mjs';
import {canonical,hash,event,metric,decode,validate,MAX_RECORD,keys,integer,isDigest,fail,SOURCES,CATEGORIES} from './schema.mjs';
const C=fs.constants, MAGIC=Buffer.from('OTRACE1\n');
const MAX_SEGMENT=16*1024*1024, MAX_INDEX=16*1024*1024;
export function frame(e){const p=Buffer.from(canonical(validate(e))),h=Buffer.alloc(36);h.writeUInt32BE(p.length);Buffer.from(hash(p),'hex').copy(h,4);return Buffer.concat([h,p]);}
export function scan(bytes){
 const events=[],problems=[];let offset=8;
 if(!bytes.subarray(0,8).equals(MAGIC))return {events,problems:['segment version/magic refused'],validBytes:0};
 while(offset<bytes.length){
  if(bytes.length-offset<36){problems.push(`torn frame header at ${offset}`);break;}
  const n=bytes.readUInt32BE(offset);if(n<2||n>MAX_RECORD){problems.push(`invalid length at ${offset}`);break;}
  if(offset+36+n>bytes.length){problems.push(`torn frame payload at ${offset}`);break;}
  const p=bytes.subarray(offset+36,offset+36+n);
  if(hash(p)!==bytes.subarray(offset+4,offset+36).toString('hex')){problems.push(`frame digest at ${offset}`);break;}
  try{events.push(decode(p));}catch{problems.push(`schema/canonical refusal at ${offset}`);break;}offset+=36+n;
 }
 return {events,problems,validBytes:offset};
}
function indexRead(root){
 const segments=new Map(),retention=[],pins=new Set(),problems=[];let next=1;
 let bytes;try{bytes=root.read('index.jsonl',MAX_INDEX);}catch(e){if(e.code==='ENOENT')return {segments,retention,pins,problems:['missing index'],next};throw e;}
 const lines=bytes.toString('utf8').split('\n');if(lines.pop()!=='')problems.push('torn index tail');
 for(const line of lines){try{
  const v=JSON.parse(line);if(canonical(v)!==line||v.schema!=='omega.trace.index.v1')fail('index schema');
  if(!/^[0-9]{8}\.seg$/.test(v.name))fail('index name');next=Math.max(next,Number(v.name.slice(0,8))+1);
  if(v.op==='add'){keys(v,['schema','op','name','bytes','digest','records','created_ms']);integer(v.bytes,8,MAX_SEGMENT);integer(v.records,1);integer(v.created_ms);if(!isDigest(v.digest)||segments.has(v.name)||retention.some(r=>r.name===v.name))fail('duplicate/bad segment');segments.set(v.name,v);}
  else if(v.op==='remove'){keys(v,['schema','op','name','reason','digest']);if(!['quota','age','purge'].includes(v.reason)||!segments.has(v.name)||segments.get(v.name).digest!==v.digest||pins.has(v.name))fail('invalid retention');retention.push(v);segments.delete(v.name);}
  else if(v.op==='pin'){keys(v,['schema','op','name','pinned']);if(typeof v.pinned!=='boolean'||!segments.has(v.name))fail('invalid pin');if(v.pinned)pins.add(v.name);else pins.delete(v.name);}
  else fail('index operation');
 }catch{problems.push('malformed index entry');break;}}
 return {segments,retention,pins,problems,next};
}
export function readArchive(rootPath){const root=new OwnedRoot(rootPath);try{
 const ix=indexRead(root),events=[],segments=[],problems=[...ix.problems],seen=new Set();
 for(const [name,entry]of ix.segments){let bytes;try{bytes=root.read(name,MAX_SEGMENT);}catch(e){if(['ENOENT','EIO'].includes(e.code)){problems.push(`${name}: missing/unreadable segment`);continue;}throw e;}
  const s=scan(bytes);if(hash(bytes)!==entry.digest||bytes.length!==entry.bytes||s.events.length!==entry.records)problems.push(`${name}: index integrity mismatch`);
  problems.push(...s.problems.map(p=>`${name}: ${p}`));segments.push({...entry,pinned:ix.pins.has(name),actual_digest:hash(bytes)});
  for(const e of s.events){if(seen.has(e.event_id)){problems.push('duplicate event identity');continue;}seen.add(e.event_id);events.push(e);}
 }
 const retained=new Set(ix.retention.map(x=>x.name));
 for(const n of root.names())if(!['index.jsonl','.writer.lock','recovery.json'].includes(n)&&!ix.segments.has(n)&&!retained.has(n))problems.push(`${n}: orphan/unknown archive file`);
 if(root.names().includes('recovery.json')){try{const note=JSON.parse(root.read('recovery.json'));if(note.schema!=='omega.trace.recovery.v1'||note.source_integrity!=='PASS')problems.push('recovered source incomplete; loss extent unknown');}catch{problems.push('invalid recovery disclosure');}}
 return {schema:'omega.trace.archive.v1',integrity:problems.length?'INCONCLUSIVE':'PASS',evidence:'NOT_VERIFIED',events,segments,retention:ix.retention,problems};
 }finally{root.close();}}
export function defaultRoot(){return path.join(process.env.XDG_STATE_HOME??path.join(process.env.HOME,'.local/state'),'omega-trace');}
function writeAll(fd,bytes){let off=0;while(off<bytes.length){const n=fs.writeSync(fd,bytes,off);if(n<=0)fail('short write');off+=n;}}
export class Archive {
 constructor(rootPath=defaultRoot(),{segmentBytes=1024*1024,clock=Date.now}={}){
  integer(segmentBytes,1024,MAX_SEGMENT);this.rootPath=rootPath;this.segmentBytes=segmentBytes;this.clock=clock;this.root=new OwnedRoot(rootPath,{create:true});
  try{
   this.lock=this.root.open('.writer.lock',C.O_WRONLY|C.O_CREAT|C.O_EXCL);writeAll(this.lock,Buffer.from(String(process.pid)));fs.fsyncSync(this.lock);
   try{const fd=this.root.open('index.jsonl',C.O_WRONLY|C.O_CREAT|C.O_EXCL);fs.fsyncSync(fd);fs.closeSync(fd);fs.fsyncSync(this.root.fd);}catch(e){if(e.code!=='EEXIST')throw e;}
   const ix=indexRead(this.root);if(ix.problems.length)fail('archive index inconclusive; read/recover before writing');this.next=ix.next;
   const r=readArchive(rootPath);if(r.integrity!=='PASS')fail('archive inconclusive; recover into a new archive');
  }catch(e){this.close();throw e;}
 }
 index(v){const fd=this.root.open('index.jsonl',C.O_WRONLY|C.O_APPEND);try{const b=Buffer.from(canonical({schema:'omega.trace.index.v1',...v})+'\n');if(fs.fstatSync(fd).size+b.length>MAX_INDEX)fail('index quota reached');writeAll(fd,b);fs.fsyncSync(fd);}finally{fs.closeSync(fd);}}
 append(events){
  if(!Array.isArray(events)||events.length>65536)fail('batch bound');
  let frames=[],size=8;const seal=()=>{if(!frames.length)return;
   if(this.next>99999999)fail('segment number overflow');const name=String(this.next++).padStart(8,'0')+'.seg',b=Buffer.concat([MAGIC,...frames]);
   const fd=this.root.open(name,C.O_WRONLY|C.O_CREAT|C.O_EXCL);try{writeAll(fd,b);fs.fsyncSync(fd);}finally{fs.closeSync(fd);}fs.fsyncSync(this.root.fd);
   this.index({op:'add',name,bytes:b.length,digest:hash(b),records:frames.length,created_ms:integer(this.clock())});frames=[];size=8;
  };
  for(const e of events){const f=frame(e);if(size+f.length>this.segmentBytes)seal();if(f.length+8>this.segmentBytes)fail('record exceeds segment size');frames.push(f);size+=f.length;}seal();
 }
 pin(name,pinned){const ix=indexRead(this.root);if(!ix.segments.has(name)||typeof pinned!=='boolean')fail('unknown pin');this.index({op:'pin',name,pinned});}
 retain({bytes=Number.MAX_SAFE_INTEGER,olderThanMs=Infinity,purge=false}={}){
  integer(bytes);if(typeof olderThanMs!=='number'||olderThanMs<0||Number.isNaN(olderThanMs))fail('invalid age');
  const ix=indexRead(this.root);if(ix.problems.length)fail('index inconclusive');let total=[...ix.segments.values()].reduce((n,s)=>n+s.bytes,0);
  const removed=[];for(const s of ix.segments.values()){
   const age=this.clock()-s.created_ms;if(ix.pins.has(s.name)||(!purge&&total<=bytes&&age<=olderThanMs))continue;
   const b=this.root.read(s.name,MAX_SEGMENT);if(hash(b)!==s.digest||scan(b).problems.length)fail('refuse to delete incomplete segment');
   // Durable tombstone first: interruption leaves a known retained-out segment, never silent loss.
   this.index({op:'remove',name:s.name,reason:purge?'purge':age>olderThanMs?'age':'quota',digest:s.digest});this.root.unlink(s.name);total-=s.bytes;removed.push(s.name);
  }return removed;
 }
 purge(){return this.retain({purge:true});}
 close(){if(this.lock!==undefined){fs.closeSync(this.lock);this.lock=undefined;try{this.root.unlink('.writer.lock');}catch{/* preserve explicit caller error on a swapped root */}}this.root?.close();}
}
export function recover(source,destination){
 if(path.resolve(source)===path.resolve(destination))fail('recovery requires new destination');
 if(fs.existsSync(destination))fail('recovery destination must be new');
 const r=readArchive(source),root=new OwnedRoot(source),events=[...r.events],seen=new Set(events.map(e=>e.event_id)),problems=[...r.problems],digests=[];
 try{for(const name of root.names().filter(n=>/^[0-9]{8}\.seg$/.test(n))){const b=root.read(name,MAX_SEGMENT),scanned=scan(b);digests.push(hash(b));problems.push(...scanned.problems.map(p=>name+': '+p));for(const e of scanned.events)if(!seen.has(e.event_id)){seen.add(e.event_id);events.push(e);}}}finally{root.close();}
 const a=new Archive(destination);try{a.append(events);const note={schema:'omega.trace.recovery.v1',source_integrity:r.integrity,recovered:events.length,loss_extent:problems.length?'unknown':'none_detected',problems,source_segments:digests};const fd=a.root.open('recovery.json',C.O_WRONLY|C.O_CREAT|C.O_EXCL);try{writeAll(fd,Buffer.from(canonical(note)));fs.fsyncSync(fd);}finally{fs.closeSync(fd);}fs.fsyncSync(a.root.fd);return {integrity:r.integrity,recovered:events.length,problems,source_segments:digests};}finally{a.close();}
}
export class BoundedWriter {
 constructor({root=defaultRoot(),enabled=false,capacity=256,segmentBytes=1024*1024}={}){
  integer(capacity,1,65536);this.enabled=enabled;this.capacity=capacity;this.pending=0;this.drops=new Map();this.errors=[];this.closed=false;
  if(enabled){this.worker=new Worker(new URL('./writer.mjs',import.meta.url),{workerData:{root,segmentBytes}});this.done=new Promise(resolve=>{this.worker.on('message',m=>{if(m.ack)this.pending--;if(m.error)this.errors.push(m.error);});this.worker.on('error',()=>this.errors.push('writer worker failed'));this.worker.on('exit',code=>{if(code)this.errors.push(`writer exit ${code}`);resolve();});});}
 }
 submit(e){if(!this.enabled||this.closed)return false;try{validate(e);}catch{this.errors.push('producer schema refused');return false;}
  this.context??=e;if(this.pending>=this.capacity||this.errors.length){const k=e.source+':'+e.category;this.drops.set(k,(this.drops.get(k)??0)+1);return false;}
  this.pending++;this.worker.postMessage({event:e});return true;
 }
 async close(){if(this.closed)return this.status();this.closed=true;if(this.worker){const losses=[];let seq=0;
  for(const [k,n]of [...this.drops].sort()){const [source,category]=k.split(':');losses.push(event({...this.context,producer:65535,sequence:String(++seq),parent:null,source:'trace',category:'loss',status:'unknown',semantic:null,realization:null,step:null,metrics:[metric('dropped',n,'events','observed')],receipts:[],session:hash('loss:'+this.context.session+source+category)}));}
  this.worker.postMessage({close:true,losses});await this.done;
 }return this.status();}
 status(){return {accepted_pending:this.pending,dropped:Object.fromEntries(this.drops),errors:[...this.errors],durable:this.closed&&!this.errors.length};}
}
