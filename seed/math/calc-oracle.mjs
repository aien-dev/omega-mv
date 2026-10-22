// SPDX-License-Identifier: AGPL-3.0-only
import fs from 'node:fs';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
const dir='evidence/math-native/calc-receipts';fs.mkdirSync(dir,{recursive:true});
const expressions=['1/3+1/6','(-7/9)*(27/14)','(2^100+3)*(2^80+5)','2^-126','9999+1','10000-1','(2/3)^-2','2^3^2','-2^2','18446744073709551615+1','(999999999999999999999999999999/7)*(7/999999999999999999999999999999)'];
const results=[];
for(let i=0;i<expressions.length;i++){
 const expr=expressions[i],r=spawnSync('aien-calc',['--no-store','eval',expr,'--json'],{encoding:'utf8',timeout:10000});
 if(r.status!==0)throw Error('calculator oracle failed '+r.stderr);
 const receipt=JSON.parse(r.stdout),value=receipt.result.value.replace(/\/1$/,'');
 if(receipt.exactness!=='exact')throw Error('non-exact oracle');
 const file=`${dir}/${String(i+1).padStart(2,'0')}.json`;fs.writeFileSync(file,r.stdout);
 const digest=spawnSync('aien-calc',['b3',file],{encoding:'utf8'});if(digest.status!==0)throw Error('receipt hash');
 const got=spawnSync('build/math-native/omega-math',[],{input:'eval '+expr+'\n',encoding:'utf8',timeout:10000});
 const ok=got.status===0&&got.stdout==='OK '+value+'\n';
 results.push({expr,receipt:file,calcReceiptId:'calc:'+digest.stdout.split(' ')[0],expected:value,got:got.stdout.trim(),status:got.status,ok});
 if(!ok)throw Error('oracle disagreement '+JSON.stringify(results.at(-1)));
}
fs.writeFileSync('evidence/math-native/calc-oracle.json',JSON.stringify(results,null,2)+'\n');
console.log(JSON.stringify({requested:expressions.length,executed:results.length,passed:results.filter(x=>x.ok).length,scriptSHA256:createHash('sha256').update(fs.readFileSync(import.meta.filename)).digest('hex')}));
