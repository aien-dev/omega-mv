// SPDX-License-Identifier: AGPL-3.0-only
// Independent BigInt reference: direct cross-products, without native cancellation.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
const abs=n=>n<0n?-n:n;
function gcd(a,b){a=abs(a);b=abs(b);while(b)[a,b]=[b,a%b];return a;}
function reduced(n,d){if(d<0n){n=-n;d=-d;}const g=gcd(n,d);return [n/g,d/g];}
const fmt=([n,d])=>`${n}${d===1n?'':'/'+d}`;
const cases=[];
// Signed denominators, zero numerator, both zero operands, and cancellation to zero.
for(const [an,ad,bn,bd] of [
 [2n,4n,3n,9n],[-2n,4n,3n,9n],[2n,4n,-3n,9n],[-2n,4n,-3n,9n],
 [2n,-4n,3n,9n],[2n,4n,3n,-9n],[2n,-4n,3n,-9n],
 [0n,7n,3n,9n],[0n,-7n,-3n,9n],[2n,4n,0n,-9n],[0n,-7n,0n,9n],
 [2n,4n,-3n,6n],[-2n,4n,-3n,6n]
]){
 for(const op of ['+','-','*','/']){
  const input=`eval (${an}/${ad})${op}(${bn}/${bd})`;
  let n,d;
  if(op==='/'&&bn===0n){cases.push({input,want:'REFUSED 2'});continue;}
  if(op==='+'){n=an*bd+bn*ad;d=ad*bd;}
  if(op==='-'){n=an*bd-bn*ad;d=ad*bd;}
  if(op==='*'){n=an*bn;d=ad*bd;}
  if(op==='/'){n=an*bd;d=ad*bn;}
  cases.push({input,want:'OK '+fmt(reduced(n,d))});
 }
}
for(const input of ['eval 1/0','eval 0/0','eval 1/(-0)','eval 0^0','eval 0^-1'])cases.push({input,want:'REFUSED 2'});
const run=spawnSync(process.argv[2]||'build/math-native/omega-math',[],{input:cases.map(c=>c.input).join('\n')+'\n',encoding:'utf8',timeout:60000});
assert.equal(run.status,0,run.stderr);
const rows=run.stdout.trimEnd().split('\n');assert.equal(rows.length,cases.length);
cases.forEach((c,i)=>{
 assert.equal(rows[i],c.want,c.input);
 if(rows[i].startsWith('OK ')){
  const text=rows[i].slice(3),[n,d=1n]=text.split('/').map(BigInt);
  assert.ok(d>0n&&gcd(n,d)===1n,c.input);
  assert.equal(fmt([n,d]),text,c.input);
 }
 console.log(JSON.stringify({...c,got:rows[i],ok:true}));
});
console.log(JSON.stringify({requested:cases.length,executed:rows.length,passed:rows.length,reference:'direct BigInt cross-products with Euclidean normalization'}));
