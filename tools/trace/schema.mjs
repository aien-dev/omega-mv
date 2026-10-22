// Offline host tooling only. No engine, authority, memory or receipt writer.
import {createHash} from 'node:crypto';
export const VERSION='omega.trace.event.v1';
export const MAX_RECORD=16384;
export const SOURCES=['train','searchtrace','argus','dual','realization','hardware','trace'];
export const CATEGORIES=['step','candidate','realization','authority','dual','hardware','loss'];
const REGISTRY={duration:['ns'],cost:['rcycles'],loss:['ratio'],rate:['per_second'],gradient:['norm'],memory:['bytes'],code_bytes:['bytes'],energy:['nJ','J'],dropped:['events'],critical_loss:['events'],prune:['code'],verdict:['code'],predicted:['ns','bytes','J','rcycles','ps','pJ','bytes_per_s','kv_blocks','ppm_occupancy','ns_verify','ns_synth','W','W_per_s','mC','mC_per_s','dimensionless','log2_ps'],uncertainty:['ns','bytes','J','rcycles','ps','pJ','bytes_per_s','kv_blocks','ppm_occupancy','ns_verify','ns_synth','W','W_per_s','mC','mC_per_s','dimensionless','log2_ps'],measured:['ns','bytes','J','rcycles','ps','pJ','bytes_per_s','kv_blocks','ppm_occupancy','ns_verify','ns_synth','W','W_per_s','mC','mC_per_s','dimensionless','log2_ps'],calibration:['code'],authority_kind:['code'],authority_status:['code'],generation:['count'],resource:['code'],actual:['code'],recommended:['code'],lost_source:['code'],lost_category:['code']};
export const METRICS=Object.freeze(Object.keys(REGISTRY));
export function fail(message){throw new Error(message);}
export function keys(o,expected){if(!o||Object.getPrototypeOf(o)!==Object.prototype||Object.keys(o).sort().join(',')!==[...expected].sort().join(','))fail('unknown or missing fields');}
export function canonical(o){
 if(o===null||typeof o==='boolean'||typeof o==='string')return JSON.stringify(o);
 if(typeof o==='number'){if(!Number.isFinite(o))fail('nonfinite number');return JSON.stringify(Object.is(o,-0)?0:o);}
 if(Array.isArray(o))return '['+o.map(canonical).join(',')+']';
 if(o&&Object.getPrototypeOf(o)===Object.prototype)return '{'+Object.keys(o).sort().map(k=>JSON.stringify(k)+':'+canonical(o[k])).join(',')+'}';
 fail('unsupported canonical value');
}
export function hash(bytes){return createHash('sha256').update(bytes).digest('hex');}
export function digest(o){return hash(canonical(o));}
export function isDigest(s){return typeof s==='string'&&/^[a-f0-9]{64}$/.test(s);}
export function uint(s){if(typeof s!=='string'||! /^(0|[1-9][0-9]{0,19})$/.test(s)||BigInt(s)>0xffffffffffffffffn)fail('invalid u64');return s;}
export function integer(n,min=0,max=Number.MAX_SAFE_INTEGER){if(!Number.isSafeInteger(n)||n<min||n>max)fail('invalid integer');return n;}
function oneOf(x,list){if(!list.includes(x))fail('unknown enumeration');}
export function metric(name,value,unit,provenance,reason=null){const m={name,value,unit,provenance,reason};checkMetric(m);return m;}
export function checkMetric(m){
 keys(m,['name','value','unit','provenance','reason']);
 if(!Object.hasOwn(REGISTRY,m.name)||!REGISTRY[m.name].includes(m.unit))fail('invalid metric or unit');
 oneOf(m.provenance,['observed','estimated','unknown']);
 if(m.provenance==='unknown'){if(m.value!==null)fail('unknown measurement must be null');oneOf(m.reason,['not_recorded','uncalibrated','stale','invalid_clock','refused']);}
 else {if(typeof m.value!=='number'||!Number.isFinite(m.value)||(m.value<0&&!['predicted','measured','loss'].includes(m.name)))fail('invalid metric value');if(m.reason!==null)fail('measured reason must be null');}
 if((m.unit==='code'||['dropped','critical_loss','generation'].includes(m.name))&&m.value!==null)integer(m.value);
 if(m.value!==null&&m.name==='lost_source')integer(m.value,0,SOURCES.length-1);
 if(m.value!==null&&m.name==='lost_category')integer(m.value,0,CATEGORIES.length-1);
 return m;
}
export function event(input){const e={schema:VERSION,privacy:'local_numeric',capture:'explicit',...input};delete e.event_id;return validate({...e,event_id:digest(e)});}
export function validate(e){
 keys(e,['schema','event_id','run','session','producer','sequence','parent','time','contract','source','category','status','qualification','semantic','realization','step','metrics','receipts','privacy','capture']);
 if(e.schema!==VERSION)fail('unknown event schema');
 for(const k of ['event_id','run','session'])if(!isDigest(e[k]))fail('invalid identity');
 for(const k of ['parent','semantic','realization'])if(e[k]!==null&&!isDigest(e[k]))fail('invalid optional identity');
 integer(e.producer,0,65535);uint(e.sequence);if(e.step!==null)integer(e.step);
 keys(e.time,['domain','tick','wall_ns']);oneOf(e.time.domain,['logical','monotonic']);uint(e.time.tick);if(e.time.wall_ns!==null)uint(e.time.wall_ns);
 keys(e.contract,['machine','build','config','precision']);for(const k of ['machine','build','config'])if(!isDigest(e.contract[k]))fail('invalid contract digest');
 oneOf(e.contract.precision,['unknown','f64','f32','bf16','f16','fp8','int8']);
 oneOf(e.source,SOURCES);oneOf(e.category,CATEGORIES);oneOf(e.status,['ok','refused','pruned','solved','rejected','unknown','uncalibrated','stale']);oneOf(e.qualification,['synthetic','observed','estimated','unknown']);
 if(e.privacy!=='local_numeric'||e.capture!=='explicit')fail('unclassified capture');
 if(!Array.isArray(e.metrics)||e.metrics.length>32)fail('metric bound');
 const names=new Set();for(const m of e.metrics){checkMetric(m);if(names.has(m.name))fail('duplicate metric');names.add(m.name);}
 if(!Array.isArray(e.receipts)||e.receipts.length>16||e.receipts.some(x=>!isDigest(x))||new Set(e.receipts).size!==e.receipts.length)fail('invalid receipt references');
 const {event_id,...payload}=e;if(digest(payload)!==event_id)fail('event digest mismatch');
 if(Buffer.byteLength(canonical(e))>MAX_RECORD)fail('record bound');return e;
}
export function decode(bytes){const s=bytes.toString('utf8'),e=JSON.parse(s);validate(e);if(canonical(e)!==s)fail('noncanonical record');return e;}
