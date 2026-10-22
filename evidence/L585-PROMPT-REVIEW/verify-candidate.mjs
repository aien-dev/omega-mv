import fs from 'node:fs';import cp from 'node:child_process';import crypto from 'node:crypto';import z from 'node:zlib';
const rev='f49e6a91e8b973c3ca0277993a663321a646451e',root='evidence/L563-PROMPT-INTERRUPT/';const get=p=>cp.execFileSync('git',['show',`${rev}:${p}`]);const hash=b=>crypto.createHash('sha256').update(b).digest('hex');let manifest=0,lossless=0;
for(const line of get(root+'SHA256SUMS').toString().trim().split('\n')){const m=line.match(/^([0-9a-f]{64})  (.*)$/);if(!m)throw Error(line);let p=m[2];if(!p.startsWith(root))p=root+p;if(hash(get(p))!==m[1])throw Error(p);manifest++;}
const pins=JSON.parse(get(root+'source-pins.json'));for(const p of pins.files)if(hash(get(p.path))!==p.sha256)throw Error(p.path);
for(const p of JSON.parse(get(root+'lossless-raw-pins.json'))){const b=get(p.packet),raw=z.gunzipSync(b);if(raw.length!==p.raw_bytes||hash(raw)!==p.raw_sha256||hash(b)!==p.gzip_sha256)throw Error(p.packet);lossless++;}
const tracked=cp.execFileSync('git',['ls-tree','-r','--name-only',rev,root],{encoding:'utf8'}).trim().split('\n').length;
const result={source:rev,manifest,tracked,inputPins:pins.files.length,lossless,status:'PASS'};fs.writeFileSync('evidence/L585-PROMPT-REVIEW/raw/candidate-git-packet.json',JSON.stringify(result,null,2)+'\n');console.log(result);
