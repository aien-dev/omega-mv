// Read-only host adapters. Native producers, rings and hook installation remain unchanged.
import fs from 'node:fs';import os from 'node:os';import path from 'node:path';import {execFileSync} from 'node:child_process';
import {event,metric,hash,validate,isDigest,fail,keys,integer,uint} from './schema.mjs';
function baseEvent(base,overrides){validate(base);return event({...base,...overrides});}
function safeCount(n){if(n>BigInt(Number.MAX_SAFE_INTEGER))fail('numeric observation exceeds exact range');return Number(n);}
function verifiedCorpus(bytes,{verifier,verifierHash}={}){
 if(!Buffer.isBuffer(bytes)||bytes.length>4*1024*1024)fail('corpus bound');
 if(!path.isAbsolute(verifier??'')||!isDigest(verifierHash))fail('pinned existing SearchTrace verifier required');
 const fd=fs.openSync(verifier,fs.constants.O_RDONLY|fs.constants.O_NOFOLLOW);let folder,input;
 try{
  const s=fs.fstatSync(fd);if(!s.isFile()||s.uid!==process.getuid()||(s.mode&0o022)||s.size>64*1024*1024||hash(fs.readFileSync(fd))!==verifierHash)fail('SearchTrace verifier pin refused');
  folder=fs.mkdtempSync(path.join(os.tmpdir(),'omega-trace-verify-'));fs.chmodSync(folder,0o700);const name=path.join(folder,'source');fs.writeFileSync(name,bytes,{mode:0o600,flag:'wx'});input=fs.openSync(name,'r');fs.unlinkSync(name);
  execFileSync('/proc/self/fd/4',['verify','/proc/self/fd/3'],{stdio:['ignore','ignore','ignore',input,fd],timeout:30000,maxBuffer:16384,env:{PATH:'/usr/bin:/bin'}});
 }catch{fail('SearchTrace canonical verifier refused source or binary');}finally{if(input!==undefined)fs.closeSync(input);fs.closeSync(fd);if(folder)fs.rmdirSync(folder);}
}
export function searchtrace(bytes,base,options){
 verifiedCorpus(bytes,options);const lines=bytes.toString('ascii').trimEnd().split('\n'),machine=lines[2].split(' ')[1],source=hash(bytes),out=[];let parents=new Map(),task=-1;
 for(const line of lines){
  if(line.startsWith('task ')){task=Number(line.split(' ')[1]);parents=new Map();continue;}
  if(!line.startsWith('step '))continue;const words=line.split(' '),seq=words[2],fields=Object.fromEntries(words.slice(3).map(w=>{const n=w.indexOf('=');return [w.slice(0,n),w.slice(n+1)];}));
  const prune=['none','type','cost','equiv','budget'].indexOf(fields.prune),verdict=['none','reject','solved','verify_fail'].indexOf(fields.verdict),parent=fields.parent==='-'?base.parent:parents.get(fields.parent);
  if(parent===undefined)fail('corpus causal parent unresolved');
  const session=hash('omega.trace.searchtrace.session.v1:'+base.session+':'+task+':'+source),semantic=fields.child==='-'?null:fields.child;
  const candidate=baseEvent(base,{source:'searchtrace',category:'candidate',session,sequence:seq,parent,time:{domain:'logical',tick:seq,wall_ns:null},step:task,contract:{...base.contract,machine},semantic,realization:null,status:prune?'pruned':verdict===2?'solved':verdict===1||verdict===3?'rejected':'ok',metrics:[metric('prune',prune,'code','observed'),metric('verdict',verdict,'code','observed')],receipts:[source]});
  out.push(candidate);parents.set(seq,candidate.event_id);
  if(fields.rrc!=='-')out.push(baseEvent(base,{source:'realization',category:'realization',session,sequence:seq,parent:candidate.event_id,time:{domain:'logical',tick:seq,wall_ns:null},step:task,contract:{...base.contract,machine},semantic,realization:null,status:Number(fields.rrc)===0?'ok':'refused',metrics:[metric('cost',Number(fields.rcycles),'rcycles','estimated'),metric('code_bytes',Number(fields.rbytes),'bytes','observed'),metric('duration',null,'ns','unknown','not_recorded')],receipts:[source]}));
 }
 return out;
}
const FLOORS=new Map();for(const n of [4,12,21,31,32,42,52,60,61,62,63,71,72,80])FLOORS.set(n,1);for(const n of [1,3,10,20,22,30,40,41,50,51,70])FLOORS.set(n,2);for(const n of [2,11,43,81])FLOORS.set(n,3);
export function argus(bytes,base){
 if(!Buffer.isBuffer(bytes)||bytes.length>8*1024*1024||bytes.length%128)fail('ARGUS truncated/bounded stream');const out=[],seqs=new Map();
 for(let p=0;p<bytes.length;p+=128){const b=bytes.subarray(p,p+128),kind=b.readUInt16LE(2),flags=b.readUInt16LE(6),sequence=b.readBigUInt64LE(8),cap=b.readUInt32LE(32),floor=FLOORS.get(kind),machine=b.subarray(64,96).toString('hex'),stream=flags>>>2,consumer=(flags&2)!==0;
  if(b[0]!==1||!floor||b[1]<1||b[1]>floor||b[4]>3||b[5]<1||b[5]>3||consumer!==(kind===80)||(cap>=256&&cap!==0xffffffff)||!sequence||sequence===0xffffffffffffffffn||(kind===81&&b[5]!==1)||([50,51,52].includes(kind)&&b[4]!==3))fail('ARGUS ABI refused');
  const key=machine+':'+stream+':'+consumer;if(sequence<=(seqs.get(key)??0n))fail('ARGUS sequence replay');seqs.set(key,sequence);
  const receipt=b.subarray(96,128).toString('hex'),metrics=[metric('authority_kind',kind,'code','observed'),metric('authority_status',b[5],'code','observed')];
  if(kind===80){const lostClass=b.readUInt32LE(36);if(lostClass<1||lostClass>4)fail('ARGUS invalid dropped class');const count=safeCount(b.readBigUInt64LE(56));metrics.push(metric('dropped',count,'events','observed'));if(lostClass===1)metrics.push(metric('critical_loss',count,'events','observed'));}
  out.push(baseEvent(base,{session:hash('omega.trace.argus.session.v1:'+base.session+key),producer:stream,sequence:String(sequence),parent:base.parent,source:'argus',category:kind===80?'loss':'authority',qualification:flags&1?'synthetic':base.qualification,contract:{...base.contract,machine},time:{domain:'logical',tick:String(b.readBigUInt64LE(16)),wall_ns:null},status:kind===80?'unknown':b[5]===1?'ok':'refused',step:null,metrics,receipts:[hash(b),...(receipt==='0'.repeat(64)?[]:[receipt])] }));
 }
 return out;
}
const UNITS={1:'ps',2:'pJ',3:'bytes',4:'bytes_per_s',5:'kv_blocks',6:'ppm_occupancy',7:'ns_verify',8:'ns_synth',9:'ns',10:'W',11:'W_per_s',12:'mC',13:'mC_per_s',14:'dimensionless',15:'log2_ps'};
export function dual(bytes,base,{state='uncalibrated'}={}){
 if(!['uncalibrated','stale','refused','unknown'].includes(state))fail('no calibration verification or fresh authorization from trace');
 if(!Buffer.isBuffer(bytes)||bytes.length<2||bytes.length>65536)fail('DUAL bounds');let off=0;
 function take(n){if(off+n>bytes.length)fail('DUAL truncation');const p=off;off+=n;return p;}
 const u8=()=>bytes[take(1)],u32=()=>bytes.readUInt32LE(take(4)),u64=()=>bytes.readBigUInt64LE(take(8)),f64=()=>{const n=bytes.readDoubleLE(take(8));if(!Number.isFinite(n))fail('DUAL nonfinite');return n;},dig=()=>{const d=bytes.subarray(off,off+32).toString('hex');take(32);if(!isDigest(d)||d==='0'.repeat(64))fail('DUAL zero digest');return d;};
 if(u8()!==5||u8()!==1)fail('DUAL version/kind refused');const site=dig(),price=dig(),generation=u64(),n=u32();integer(n,1,64);const candidates=Array.from({length:n},dig),actual=u32(),recommended=u32(),count=u32();integer(actual,0,n-1);integer(recommended,0,n-1);integer(count,0,32);
 const rows=[];let previous=-1;for(let i=0;i<count;i++){const resource=u32(),unit=UNITS[u32()],predicted=f64(),sd=f64(),measuredFlag=u32(),measured=f64(),error=f64();if(resource<=previous||!unit||sd<0||![0,1].includes(measuredFlag)||(measuredFlag&&(sd===0||error!==(measured-predicted)/sd))||(!measuredFlag&&(measured!==0||error!==0)))fail('DUAL consequence refused');previous=resource;rows.push({resource,unit,predicted,sd,measuredFlag,measured});}
 if(u32()!==0||off!==bytes.length)fail('DUAL authority/trailing bytes refused');
 const receipt=hash(Buffer.concat([Buffer.from('omega.dual.recommendation.v1\0'),bytes])),source=hash(bytes);
 // Preserve advisory status; estimates never acquire measurement or calibration authority.
 return (rows.length?rows:[null]).map((r,i)=>baseEvent(base,{source:'dual',category:'dual',status:state,session:hash('omega.trace.dual.session.v1:'+base.session+source),sequence:String(i),semantic:site,realization:candidates[actual],time:{domain:'logical',tick:String(generation),wall_ns:null},metrics:r?[metric('resource',r.resource,'code','observed'),metric('actual',actual,'code','observed'),metric('recommended',recommended,'code','observed'),metric('predicted',r.predicted,r.unit,'estimated'),metric('uncertainty',r.sd,r.unit,'estimated'),metric('measured',r.measuredFlag?r.measured:null,r.unit,r.measuredFlag?'observed':'unknown',r.measuredFlag?null:'not_recorded'),metric('calibration',null,'code','unknown',state==='stale'?'stale':state==='refused'?'refused':'uncalibrated')]:[],receipts:[receipt,source,price]}));
}
