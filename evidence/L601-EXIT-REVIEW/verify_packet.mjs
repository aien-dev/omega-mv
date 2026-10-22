// Verify publication from Git objects, including every archived raw member.
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {gunzipSync} from 'node:zlib';
const ref=process.argv[2]??'HEAD', root='evidence/L601-EXIT-REVIEW/';
const sha=b=>createHash('sha256').update(b).digest('hex');
const blob=p=>{const r=spawnSync('git',['show',ref==='--staged'?':'+p:ref+':'+p],{maxBuffer:64<<20});if(r.status!==0)throw Error('missing Git object '+p);return r.stdout};
const manifest=blob(root+'SHA256SUMS').toString(); let payloads=0;
for(const line of manifest.trim().split('\n')){
 const m=/^([a-f0-9]{64})  (.+)$/.exec(line); if(!m)throw Error('invalid manifest line');
 if(sha(blob(root+m[2]))!==m[1])throw Error('Git payload mismatch '+m[2]); payloads++;
}
const pins=JSON.parse(blob(root+'source-pins.json'));
for(const [p,hash] of Object.entries(pins.files)){
 const r=spawnSync('git',['show',pins.sourceHead+':'+p],{maxBuffer:16<<20});
 if(r.status!==0||sha(r.stdout)!==hash)throw Error('pinned source mismatch '+p);
}
const expected=JSON.parse(blob(root+'raw-members.json')), found=new Map();
const tar=gunzipSync(blob(root+'raw.tar.gz'));
const field=(h,a,n)=>h.subarray(a,a+n).toString().replace(/\0.*$/s,'');
for(let i=0;i+512<=tar.length;){
 const h=tar.subarray(i,i+512); if(h.every(b=>b===0))break;
 const name=field(h,0,100),prefix=field(h,345,155),type=field(h,156,1);
 const path=(prefix?prefix+'/':'')+name;
 const size=parseInt(field(h,124,12).trim()||'0',8);
 if(!Number.isSafeInteger(size)||size<0)throw Error('invalid tar size');
 const data=tar.subarray(i+512,i+512+size);if(data.length!==size)throw Error('truncated member');
 if(type==='0'||type===''){
  const p=path.replace(/^\.\//,'');if(found.has(p))throw Error('duplicate member '+p);
  found.set(p,{bytes:size,sha256:sha(data)});
 } else if(type!=='5')throw Error('unsupported archive member '+type);
 i+=512+Math.ceil(size/512)*512;
}
if(found.size!==Object.keys(expected).length)throw Error('raw member count mismatch');
for(const [p,pin] of Object.entries(expected)){
 const got=found.get(p);if(!got||got.bytes!==pin.bytes||got.sha256!==pin.sha256)throw Error('raw member mismatch '+p);
}
console.log(`GIT_PACKET_PASS ref=${ref} payloads=${payloads} raw_members=${found.size} pinned_sources=${Object.keys(pins.files).length}`);
