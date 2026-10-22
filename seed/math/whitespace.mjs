// SPDX-License-Identifier: AGPL-3.0-only
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import {createHash} from 'node:crypto';
const fixtures=[
 ['eval\r1\n','OK 1\n'],
 ['eval \r1\n','OK 1\n'],['eval\t1\n','OK 1\n'],
 ['eval\r\t \r1\r\n','OK 1\n'],['eval\r1','OK 1\n'],
 ['eval\r1\r','OK 1\n'],['eval\r1+\r2\n','OK 3\n'],
 ['eval\r1\r2\n','REFUSED 1\n'],['eval\r\n','REFUSED 1\n'],
 ['eval\n','REFUSED 1\n'],['eval1\n','REFUSED 1\n'],
 ['eval\v1\n','REFUSED 1\n'],['eval\f1\n','REFUSED 1\n'],
 ['eval\r1@\r\n','REFUSED 1\n'],['eval\r1;2\r\n','REFUSED 1\n'],
 ['eval\r1/0\r\neval\r7/9\r\n','REFUSED 2\nOK 7/9\n'],
 ['eval\r1@\r\neval\r2\r\n','REFUSED 1\nOK 2\n'],
 ['x'.repeat(5000)+'\r\neval\r2\r\n','REFUSED 4\nOK 2\n'],
 ['eval\r1'+' '.repeat(4089)+'\n','OK 1\n'],
 ['eval\r1'+' '.repeat(4090)+'\neval\r3\n','REFUSED 4\nOK 3\n'],
];
for(const [cmd,expr,want] of [
 ['expand','(x+1)^2','1+2*x+x^2'],['collect','x+x','2*x'],
 ['diff','x^2','2*x'],['integ','2*x','x^2'],['gcd','x^2-1;x-1','-1+x'],
 ['subst','x+1;2','3'],['solve','x+1;2','1'],['linear','1;0;0;1;2;3','2;3']
])fixtures.push([cmd+'\r'+expr+'\r\n','OK '+want+'\n']);
let failures=0,executed=0;
for(const [input,want] of fixtures){
 const r=spawnSync(process.argv[2]||'build/math-native/omega-math',[],{input,encoding:'utf8',timeout:10000});
 assert.equal(r.status,0,r.stderr);executed++;
 const ok=r.stdout===want;if(!ok)failures++;
 console.log(JSON.stringify({input,want,got:r.stdout,status:r.status,ok}));
}
console.log(JSON.stringify({requested:fixtures.length,executed,passed:executed-failures,failed:failures,sourceHash:createHash('sha256').update(fs.readFileSync(import.meta.filename)).digest('hex')}));
assert.equal(failures,0,'command boundary must accept space/tab/CR consistently and recover after refusal');
