import {spawnSync} from 'node:child_process';
import {unit,expected} from './fixtures.mjs';
let rows=[];
for(let [command,mode] of [['encode',1],['decode',2],['identity',3],['provenance',4],['replay',5]]){
 const input=unit(),r=spawnSync('sh',['tools/source-aware/sa',command],{input});
 rows.push({command,status:r.status,ok:r.status===0&&r.stdout.equals(expected(mode,input))});
}
console.log(JSON.stringify({phase:'RED',requested:rows.length,executed:rows.length,passed:rows.filter(r=>r.ok).length,rows},null,2));
process.exit(rows.every(r=>r.ok)?0:1);
