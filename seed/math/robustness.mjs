// SPDX-License-Identifier: AGPL-3.0-only
import fs from 'node:fs';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
let s=494;const rand=n=>{s=(Math.imul(s,1664525)+1013904223)>>>0;return s%n;};
const chars='0123456789x+-*/^();[]{} .abcdef\t\r\u0000';
let requests=[];
for(let i=0;i<600;i++){
 let v='eval ';for(let j=0,n=rand(80);j<n;j++)v+=chars[rand(chars.length)];
 requests.push(v+'@','eval 1/3+1/6');
}
const exe='build/math-native/omega-math',input=requests.join('\n')+'\n';
const r=spawnSync(exe,[],{input,encoding:'utf8',timeout:60000});
if(r.status!==0)throw Error('malformed stream trap/crash '+r.status);
const rows=r.stdout.trimEnd().split('\n');
if(rows.length!==requests.length)throw Error('missing output');
rows.forEach((got,i)=>{const ok=i%2?got==='OK 1/2':/^REFUSED [1-6]$/.test(got);console.log(JSON.stringify({input:requests[i],got,ok}));if(!ok)throw Error('accepted malformed or failed recovery');});
console.log(JSON.stringify({requested:requests.length,executed:rows.length,malformedRefused:600,recoveryPassed:600,inputSHA256:createHash('sha256').update(input).digest('hex')}));
