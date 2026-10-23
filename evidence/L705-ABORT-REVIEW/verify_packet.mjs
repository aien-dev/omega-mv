import {spawnSync} from 'node:child_process';import {gunzipSync} from 'node:zlib';import {createHash} from 'node:crypto';
const ref=process.argv[2]??'HEAD',root='evidence/L705-ABORT-REVIEW/',sha=b=>createHash('sha256').update(b).digest('hex');
function git(p,r=ref){const x=spawnSync('git',['show',r==='--staged'?':'+p:r+':'+p],{maxBuffer:32<<20});if(x.status)throw Error('missing Git blob '+p);return x.stdout;}
const sums=git(root+'SHA256SUMS').toString().trim().split('\n'),seen=new Set();for(const l of sums){const m=l.match(/^([a-f0-9]{64})  (.+)$/);if(!m||seen.has(m[2])||m[2].includes('..')||m[2].startsWith('/'))throw Error('invalid manifest');seen.add(m[2]);if(sha(git(root+m[2]))!==m[1])throw Error('payload '+m[2]);}
const pins=JSON.parse(git(root+'source-pins.json'));for(const [p,h]of Object.entries(pins.files))if(sha(git(p,pins.head))!==h)throw Error('source '+p);
const raws=JSON.parse(git(root+'raw-pins.json'));for(const [p,h]of Object.entries(raws)){const b=gunzipSync(git(root+p));if(b.length!==h.bytes||sha(b)!==h.sha256)throw Error('raw '+p);}
const report=JSON.parse(git(root+'L705-review.json'));if(report.runtime_verdict!=='ACCEPT_HOST_SCOPE'||report.dispatch_verdict!=='HOLD'||report.integration_verdict!=='HOLD_NOT_APPROVED'||report.status!=='OPEN_FOR_PARENT_DISPOSITION')throw Error('disposition drift');
console.log(JSON.stringify({result:'GIT_PACKET_PASS',ref,payloads:sums.length,raw_records:Object.keys(raws).length,source_pins:Object.keys(pins.files).length,integration:'HOLD_NOT_APPROVED'}));
